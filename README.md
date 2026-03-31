<div align="center">

# InterRec

**Windows 游戏进程内录制器**

*DX11 / DX12 / Vulkan Layer | GPU 帧捕获 | Media Foundation 编码 | 共享内存 IPC*

![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![Graphics](https://img.shields.io/badge/Graphics-DX11%20%7C%20DX12%20%7C%20Vulkan-green?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

</div>

---

> [!NOTE]
> **图形路径**  
> 代码层统一通过 `Universal-Render-Hook` 接入 DX11 / DX12 / Vulkan，不直接引用 VulkanHook。

> [!IMPORTANT]
> **控制模型**  
> 宿主发布可用后端，控制器在可用后端中单选录制后端，录制期间锁定直到停止或进程结束。

## 特性

| 功能 | 说明 |
|:-----|:-----|
| **DX11 帧捕获** | Present 路径复制 BackBuffer 到 staging texture，Map 读回 |
| **DX12 帧捕获** | 复制 BackBuffer 到 readback buffer，fence 等待完成 |
| **Vulkan 帧捕获** | 独立 readback slot + fence 轮询收集已完成帧 |
| **MF 编码** | `IMFSinkWriter` 写入 H.264 MP4 |
| **进程音频捕获** | WASAPI loopback 捕获与混流 |
| **QPC 时间戳** | `QueryPerformanceCounter` 驱动音视频时间轴 |
| **共享内存 IPC** | Controller 与 Hook 通过共享内存交换状态、日志、预览、命令 |
| **后端单选** | 可用后端中单选录制后端，录制期间锁定 |
| **预览发布** | 可选发布当前帧给控制器预览 |

---

## 架构

```
PluginVideoRecordController.exe
  SessionRegistry
    ├─ SharedState    (状态 / 可用后端 / 所选后端)
    ├─ PreviewFrame   (预览图像)
    ├─ LogBuffer      (日志环形缓冲)
    └─ CommandEvent   (开始 / 停止)
          │
          │ shared memory + event
          ▼
PluginVideoRecordHook.dll
  ├─ Host
  │   └─ URH AutoHook (DX11 / DX12 / Vulkan)
  ├─ VideoRecorder
  │   ├─ Dx11Capture
  │   ├─ Dx12Capture
  │   └─ VulkanCapture
  ├─ ProcessAudioCapture
  ├─ MfWriter
  └─ PreviewPublisher
```

---

## 后端选择流程

```
宿主轮询 Hook / Layer 状态
    ├─ 刷新 availableBackendMask
    │   ├─ DX11 是否命中
    │   ├─ DX12 是否命中
    │   └─ Vulkan Layer 是否可用
    └─ 控制器读取状态
        ├─ 未选择时默认优先级：Vulkan > DX12 > DX11
        ├─ 已选择时只允许在可用后端中选择
        └─ 开始录制后锁定 selectedBackend
```

注意事项：
- 未启用 Layer 模式时，Vulkan 不作为可录制后端
- 启用 Layer 后需重启目标进程
- 控制流：探测 → 单选 → 锁定

---

## IPC 与会话

基于共享对象组合，非命名管道协议：

```
SessionRegistry      → controller 与 target 租约槽
SharedState          → connectionState / recorderState / backends / outputPath / error
PreviewFrame         → 最新预览帧
LogBuffer            → 固定大小环形缓冲
CommandEvent         → StartRecording / StopRecording
```

命令流：

```
Controller
  ├─ 写入 commandType / commandSequence
  └─ SetEvent(CommandEvent)

Hook
  ├─ 观察 commandSequence 变化
  ├─ 在所选后端处理命令
  └─ 写回 acknowledgedSequence
```

---

## 帧捕获流程

### DX11

```
Present
    ├─ swapChain->GetBuffer(0, &backBuffer)
    ├─ CopyResource(stagingTexture, backBuffer)
    ├─ Map(stagingTexture) → 复制像素 → Unmap
```

### DX12

```
Present
    ├─ 获取 BackBuffer
    ├─ 录制 copy 命令
    ├─ ExecuteCommandLists() → Signal(fence)
    ├─ WaitForSingleObject(fence)
    ├─ readbackBuffer->Map() → 复制像素
```

### Vulkan

```
vkQueuePresentKHR
    ├─ CollectCompletedFrame()
    │   ├─ 扫描 submitted readback slot
    │   ├─ vkWaitForFences(timeout=0)
    │   └─ 转换像素交给编码器
    └─ SubmitCapture()
        ├─ 选择空闲 slot
        ├─ 录制 image → buffer copy
        └─ vkQueueSubmit(slot.fence)
```

稳态路径不使用 `vkQueueWaitIdle()`。

---

## Media Foundation 编码

```
初始化
    ├─ MFCreateSinkWriterFromURL(outputPath)
    ├─ 设置视频输出 (H.264) / 输入 (RGB32/NV12)
    ├─ 设置音频 (AAC)
    └─ BeginWriting()

每帧
    ├─ MFCreateMemoryBuffer → 复制像素
    ├─ MFCreateSample()
    ├─ SetSampleTime / SetSampleDuration
    └─ WriteSample()
```

---

## 目录结构

```
InterRec/
├── PluginVideoRecordHook/        # 录制宿主 DLL
├── PluginVideoRecordController/  # Win32 控制器
├── PluginVideoRecordVkLayer/     # Vulkan Layer DLL
├── PluginVideoRecordLoaderMono/  # Mono 注入器
├── PluginVideoRecordLoaderIl2Cpp/# IL2CPP 注入器
├── PluginVideoRecordLoaderShared/# 共享注入逻辑
├── Shared/                       # 公共头文件
├── DependencyRoots.props         # 依赖路径配置
└── PluginVideoRecord.sln
```

---

## 构建

Visual Studio 2022 Developer Command Prompt：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64
```

覆盖依赖根目录：

```powershell
msbuild ... /p:DependencyRoot=F:\YourWorkspace\
```

分别覆盖：

```powershell
msbuild ... /p:UrhRoot=F:\Deps\URH\ /p:VkhRoot=F:\Deps\VulkanHook\
```

---

## 运行时产物

| 产物 | 说明 |
|:-----|:-----|
| `PluginVideoRecordHook.dll` | 目标进程注入的录制宿主 |
| `PluginVideoRecordController.exe` | Win32 控制器 |
| `PluginVideoRecordVkLayer.dll` | Vulkan Layer；缺少时禁用 Layer 模式 |
| `PluginVideoRecordVkLayer.json` | 控制器运行时生成的 Layer manifest |

---

## 依赖方向

```
Universal-Render-Hook ← InterRec
```

---

<div align="center">

**Platform:** Windows x64 | **License:** MIT

</div>
