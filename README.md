<div align="center">

# InterRec

**Windows 游戏进程内录制器**

*DX11 / DX12 / Vulkan Layer | GPU 帧捕获 | Media Foundation 编码 | 共享内存控制器*

![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![Graphics](https://img.shields.io/badge/Graphics-DX11%20%7C%20DX12%20%7C%20Vulkan-green?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

</div>

---

> [!NOTE]
> **当前图形路径**  
> `DX11 / DX12` 通过 `Universal-Render-Hook` 接入。  
> `Vulkan` 只保留 `Layer` 路径，通过 `VulkanHook` 接入；当前仓库里已经没有正式的非 Layer Vulkan 录制路径。

> [!IMPORTANT]
> **当前控制模型**  
> 宿主负责发布“当前可用后端”，控制器负责在可用后端中选择一个录制后端。  
> 录制开始后所选后端会被锁定，直到手动停止或目标进程结束。

## 特性

| 功能 | 说明 |
|:-----|:-----|
| **DX11 帧捕获** | 在 `Present` 路径获取 `IDXGISwapChain`，复制 BackBuffer 到 staging texture，再 `Map` 读回 |
| **DX12 帧捕获** | 复制 BackBuffer 到 readback buffer，并通过 fence 等待这次 copy 完成 |
| **Vulkan 帧捕获** | 把 copy 命令提交到独立 readback slot，之后通过每个 slot 的 fence 轮询收集已完成帧 |
| **Media Foundation 编码** | 使用 `IMFSinkWriter` 写入 H.264 MP4 |
| **进程音频捕获** | 基于 WASAPI / loopback 的音频捕获与混流 |
| **QPC 时间戳** | 使用 `QueryPerformanceCounter` 驱动音视频时间轴 |
| **共享内存控制器** | Controller 与 Hook 通过共享内存映射和事件交换状态、日志、预览与命令 |
| **后端单选** | 控制器只在“当前可用后端”中允许单选一个录制后端，录制期间锁定 |
| **预览发布** | 可选把当前视频帧发布给控制器预览页 |

---

## 架构

```text
PluginVideoRecordController.exe
  SessionRegistry
    ├─ SharedState    (在线状态 / 录制状态 / 可用后端 / 所选后端)
    ├─ PreviewFrame   (预览图像共享内存)
    ├─ LogBuffer      (日志环形缓冲)
    └─ CommandEvent   (开始 / 停止命令)
          │
          │ shared memory + event
          ▼
PluginVideoRecordHook.dll
  ├─ PluginVideoRecordHost
  │   ├─ URH AutoHook       (DX11 / DX12)
  │   └─ VHK Layer Runtime  (Vulkan)
  ├─ PluginVideoRecordVideoRecorder
  │   ├─ Dx11Capture
  │   ├─ Dx12Capture
  │   └─ VulkanCapture
  ├─ PluginVideoRecordProcessAudioCapture
  ├─ PluginVideoRecordMfWriter
  └─ PluginVideoRecordPreviewPublisher
```

---

## 后端选择流程

```text
宿主轮询 Hook / Layer 状态
    │
    ├─ 刷新 availableBackendMask
    │   ├─ DX11 是否命中
    │   ├─ DX12 是否命中
    │   └─ Vulkan Layer 是否识别并可用
    │
    └─ 控制器读取状态
        │
        ├─ 如果用户尚未明确选择
        │   └─ 默认优先级：Vulkan > DX12 > DX11
        │
        ├─ 如果用户已选择
        │   └─ 只允许在当前可用后端里选择
        │
        └─ 开始录制后锁定 selectedBackend
```

需要注意：

- 未启用 Layer 模式时，控制器不会把 `Vulkan` 当成可录制后端。
- 启用 Layer 模式后，目标进程需要重启，`Vulkan` 才会进入识别链。
- 当前正式控制流不是“自动在三后端之间切换录制”，而是“先探测，再单选，再锁定”。

---

## IPC 与会话

当前实现不是命名管道请求/响应协议，而是以下共享对象组合：

```text
SessionRegistry
  └─ controller 与 target 的租约槽

SharedState
  ├─ connectionState
  ├─ recorderState
  ├─ availableBackendMask
  ├─ selectedBackend
  ├─ activeRecordingBackend
  ├─ currentOutputPath
  └─ lastErrorMessage

PreviewFrame
  └─ 最新预览帧

LogBuffer
  └─ 固定大小日志环形缓冲

CommandEvent
  └─ StartRecording / StopRecording
```

命令流如下：

```text
Controller
  ├─ 写入 commandType / commandSequence
  └─ SetEvent(CommandEvent)

Hook
  ├─ 观察 commandSequence 变化
  ├─ 在所选后端上处理命令
  └─ 写回 acknowledgedSequence / lastHandledCommandType
```

---

## 帧捕获流程

### DX11 捕获

```text
Present 路径
    │
    ├─ swapChain->GetBuffer(0, &backBuffer)
    ├─ deviceContext->CopyResource(stagingTexture, backBuffer)
    ├─ deviceContext->Map(stagingTexture, &mappedResource)
    ├─ 逐行复制像素到编码输入缓冲
    └─ deviceContext->Unmap(stagingTexture)
```

### DX12 捕获

```text
Present 路径
    │
    ├─ 获取当前 BackBuffer
    ├─ 录制 copy 命令到 commandList
    ├─ commandQueue->ExecuteCommandLists()
    ├─ commandQueue->Signal(fence, value)
    ├─ fence->SetEventOnCompletion() + WaitForSingleObject()
    ├─ readbackBuffer->Map()
    └─ 复制像素到编码输入缓冲
```

### Vulkan 捕获

```text
vkQueuePresentKHR 路径
    │
    ├─ CollectCompletedFrame()
    │   ├─ 扫描所有 submitted readback slot
    │   ├─ 对每个 slot 做 vkWaitForFences(..., timeout=0)
    │   ├─ 找到最早完成的 slot
    │   └─ 转换像素并交给编码器
    │
    └─ SubmitCapture()
        ├─ 选择一个空闲 readback slot
        ├─ 录制 image -> buffer copy 命令
        ├─ vkQueueSubmit(..., slot.fence)
        └─ 标记 slot 已提交
```

稳态路径不使用 `vkQueueWaitIdle()`。

---

## Media Foundation 编码流程

```text
MfWriter 初始化
    │
    ├─ MFCreateSinkWriterFromURL(outputPath)
    ├─ 设置视频输出类型 (H.264)
    ├─ 设置视频输入类型 (RGB32 / NV12)
    ├─ 设置音频类型 (AAC)
    └─ BeginWriting()

每帧写入
    │
    ├─ MFCreateMemoryBuffer(frameSize)
    ├─ 复制像素到 buffer
    ├─ MFCreateSample()
    ├─ SetSampleTime(sampleTimeHns)
    ├─ SetSampleDuration(frameDurationHns)
    └─ WriteSample()
```

---

## 目录结构

```text
InterRec/
├── PluginVideoRecordHook/
├── PluginVideoRecordController/
├── PluginVideoRecordVkLayer/
├── PluginVideoRecordLoaderMono/
├── PluginVideoRecordLoaderIl2Cpp/
├── PluginVideoRecordLoaderShared/
├── Shared/
├── DependencyRoots.props
├── PluginVideoRecord.sln
├── LICENSE
└── README.md
```

---

## 构建

在 **Visual Studio 2022 Developer Command Prompt** 中执行：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64
```

如果依赖目录不是兄弟仓库结构，可以覆盖依赖根目录：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64 /p:DependencyRoot=F:\YourWorkspace\
```

或分别覆盖：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64 /p:UrhRoot=F:\Deps\Universal-Render-Hook\URH\ /p:VkhRoot=F:\Deps\VulkanHook\VulkanHook\
```

---

## 运行时产物

- `PluginVideoRecordHook.dll`
  - 目标进程注入的录制宿主
- `PluginVideoRecordController.exe`
  - Win32 控制器
- `PluginVideoRecordVkLayer.dll`
  - Vulkan Layer 组件；缺少它时，控制器会禁用 Layer 模式开关
- `PluginVideoRecordVkLayer.json`
  - 由控制器在运行时生成和注册的 Layer manifest，不作为固定仓库文件跟踪

---

## 依赖方向

```text
VulkanHook      Universal-Render-Hook
      ↑                 ↑
      └────────┬────────┘
               ↑
            InterRec
```

- `InterRec -> Universal-Render-Hook`
  - DX11 / DX12 Hook、运行时快照、AutoHook 诊断
- `InterRec -> VulkanHook`
  - Vulkan Layer 交付、controller 侧 Layer 开关与 manifest 管理
- `Universal-Render-Hook -> VulkanHook`
  - 当 `AutoHook` 需要把 Vulkan 纳入候选时，通过 `VulkanHook` 获取 Vulkan runtime 与回调

---

<div align="center">

**Platform:** Windows x64 | **License:** MIT

</div>
