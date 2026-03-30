<div align="center">

# InterRec

**Windows 游戏进程内录制工具**

*DX11 / DX12 / Vulkan-Layer | GPU 帧捕获 | Media Foundation 编码 | IPC 控制*

![C++](https://img.shields.io/badge/C%2B%2B-17-blue?style=flat-square)
![Platform](https://img.shields.io/badge/Platform-Windows%20x64-lightgrey?style=flat-square)
![Graphics](https://img.shields.io/badge/Graphics-DX11%20%7C%20DX12%20%7C%20Vulkan-green?style=flat-square)
![License](https://img.shields.io/badge/License-MIT-yellow?style=flat-square)

</div>

---

> [!NOTE]
> **图形路径说明**  
> `DX11 / DX12` 走 `Universal-Render-Hook`，`Vulkan` 走标准 `Layer` 链。  
> 当前正式 Vulkan 路径只保留 `Layer` 模式。

> [!IMPORTANT]
> **仓库定位**  
> 本仓库只放录制业务本体：抓帧、音频、编码、IPC、控制器。  
> 底层图形 Hook 能力已经拆到 `Universal-Render-Hook` 与 `VulkanHook`。

---

## 特性

| 功能 | 说明 |
|:-----|:-----|
| **DX11 帧捕获** | 在 `Present` Hook 中获取 `IDXGISwapChain`，通过 `GetBuffer` 取得 BackBuffer，使用 `ID3D11DeviceContext::CopyResource` 复制到 Staging Texture，再 `Map` 读取像素数据 |
| **DX12 帧捕获** | Hook `Present` 后获取 `ID3D12Resource` (BackBuffer)，创建 Readback Heap，使用 `CopyTextureRegion` 复制数据，通过 `ID3D12Fence` 同步 GPU 完成后 `Map` 读取 |
| **Vulkan 帧捕获** | 在 `vkQueuePresentKHR` 拦截后，创建 Staging Buffer（`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT`），使用 `vkCmdCopyImageToBuffer` 复制 Swapchain Image，`vkMapMemory` 读取像素 |
| **Media Foundation 编码** | 使用 `IMFSinkWriter` 将 RGB/NV12 帧写入 H.264 MP4，支持硬件加速（Intel QSV / NVIDIA NVENC / AMD VCE） |
| **进程音频捕获** | 通过 WASAPI Loopback 捕获系统音频，或使用 `IAudioClient` 捕获进程内音频，与视频帧同步混流 |
| **QPC 时间戳** | 使用 `QueryPerformanceCounter` 记录每帧精确时间，计算 `SampleTime` 保证音视频同步 |
| **IPC 控制** | `PluginVideoRecordController` 通过命名管道与注入的 DLL 通信，发送 Start / Stop / Query 命令 |
| **Session 管理** | `SessionRegistry` 管理多个游戏进程的录制会话，每个会话独立状态机 |
| **后端自动切换** | `PluginVideoRecordHost` 监听 `URH` 后端仲裁结果，自动在 DX11 / DX12 / Vulkan 之间切换捕获路径 |
| **预览发布** | `PluginVideoRecordPreviewPublisher` 可选将捕获帧通过共享内存发布给外部预览程序 |

---

## 架构

```
PluginVideoRecordController (Win32 控制器)
  SessionRegistry → 命名管道 IPC → Start / Stop / Query
    |
    | IPC
    v
PluginVideoRecordHook.dll (注入目标进程)
  |
  +-- PluginVideoRecordHost
        |
        +-- URH AutoHook (DX11/DX12)
        +-- VHK Layer (Vulkan)
        +-- IpcServer (命名管道)
              |
              v
        PluginVideoRecordVideoRecorder
          +-- Dx11Capture
          +-- Dx12Capture
          +-- VulkanCapture
                |
                v
              MfWriter (Media Foundation)
                |
                +-- ProcessAudioCapture (WASAPI)
                |
                v
              Output.mp4
```

---

## 帧捕获流程

### DX11 捕获

```
Present Hook 触发
    │
    ├─ swapChain->GetBuffer(0, &backBuffer)
    │
    ├─ 创建 Staging Texture (D3D11_USAGE_STAGING)
    │   ├─ Format: DXGI_FORMAT_B8G8R8A8_UNORM
    │   ├─ Usage: D3D11_USAGE_STAGING
    │   └─ CPUAccessFlags: D3D11_CPU_ACCESS_READ
    │
    ├─ deviceContext->CopyResource(stagingTexture, backBuffer)
    │
    ├─ deviceContext->Map(stagingTexture, &mappedResource)
    │   └─ 获取 pData / RowPitch
    │
    ├─ 逐行复制像素到 MfWriter 输入缓冲
    │
    └─ deviceContext->Unmap(stagingTexture)
```

### DX12 捕获

```
Present Hook 触发
    │
    ├─ swapChain->GetCurrentBackBufferIndex()
    │
    ├─ 获取 BackBuffer (ID3D12Resource)
    │
    ├─ 创建 Readback Buffer
    │   ├─ HeapType: D3D12_HEAP_TYPE_READBACK
    │   └─ Size: width * height * 4 (BGRA)
    │
    ├─ commandList->ResourceBarrier(RENDER_TARGET → COPY_SOURCE)
    │
    ├─ commandList->CopyTextureRegion(readbackBuffer, backBuffer)
    │
    ├─ commandList->ResourceBarrier(COPY_SOURCE → RENDER_TARGET)
    │
    ├─ commandQueue->ExecuteCommandLists()
    │
    ├─ commandQueue->Signal(fence, fenceValue)
    │
    ├─ fence->SetEventOnCompletion() + WaitForSingleObject()
    │   └─ GPU 完成后继续
    │
    ├─ readbackBuffer->Map(&pData)
    │
    ├─ 复制像素到 MfWriter
    │
    └─ readbackBuffer->Unmap()
```

### Vulkan 捕获

```
vkQueuePresentKHR 拦截
    │
    ├─ 获取当前 Swapchain Image
    │
    ├─ 创建 Staging Buffer
    │   ├─ Usage: VK_BUFFER_USAGE_TRANSFER_DST_BIT
    │   └─ Memory: VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    │
    ├─ vkCmdPipelineBarrier(IMAGE_LAYOUT_PRESENT_SRC → TRANSFER_SRC)
    │
    ├─ vkCmdCopyImageToBuffer(swapchainImage → stagingBuffer)
    │
    ├─ vkCmdPipelineBarrier(TRANSFER_SRC → PRESENT_SRC)
    │
    ├─ vkQueueSubmit() + vkQueueWaitIdle()
    │
    ├─ vkMapMemory(stagingBuffer, &pData)
    │
    ├─ 复制像素到 MfWriter
    │
    └─ vkUnmapMemory()
```

---

## Media Foundation 编码流程

```
MfWriter 初始化
    │
    ├─ MFCreateSinkWriterFromURL(outputPath, &sinkWriter)
    │
    ├─ 设置视频输出类型
    │   ├─ MF_MT_MAJOR_TYPE: MFMediaType_Video
    │   ├─ MF_MT_SUBTYPE: MFVideoFormat_H264
    │   ├─ MF_MT_FRAME_SIZE: width / height
    │   ├─ MF_MT_FRAME_RATE: 60 / 1
    │   └─ MF_MT_AVG_BITRATE: 8000000 (8 Mbps)
    │
    ├─ 设置视频输入类型
    │   ├─ MF_MT_SUBTYPE: MFVideoFormat_RGB32 或 MFVideoFormat_NV12
    │   └─ MF_MT_DEFAULT_STRIDE: width * 4
    │
    ├─ 设置音频类型 (可选)
    │   ├─ MF_MT_SUBTYPE: MFAudioFormat_AAC
    │   └─ MF_MT_AUDIO_SAMPLES_PER_SECOND: 48000
    │
    └─ sinkWriter->BeginWriting()

每帧写入
    │
    ├─ MFCreateMemoryBuffer(frameSize, &buffer)
    │
    ├─ buffer->Lock(&pData)
    │   └─ 复制像素数据
    │
    ├─ buffer->Unlock()
    │
    ├─ MFCreateSample(&sample)
    │
    ├─ sample->AddBuffer(buffer)
    │
    ├─ sample->SetSampleTime(sampleTimeHns)    # 100ns 单位
    │
    ├─ sample->SetSampleDuration(frameDurationHns)
    │
    └─ sinkWriter->WriteSample(videoStreamIndex, sample)
```

---

## IPC 协议

```
Controller → Hook (命名管道: \\.\pipe\PluginVideoRecord_{PID})

请求格式:
  [ CommandId (4 bytes) | PayloadLen (4 bytes) | Payload (variable) ]

Commands:
  CMD_START_RECORD  (1): 开始录制
  CMD_STOP_RECORD   (2): 停止录制
  CMD_QUERY_STATE   (3): 查询状态
  CMD_SET_CONFIG    (4): 设置配置

响应格式:
  [ ResultCode (4 bytes) | PayloadLen (4 bytes) | Payload (variable) ]

Results:
  RESULT_OK         (0): 成功
  RESULT_ERROR      (1): 失败
  RESULT_BUSY       (2): 录制中
  RESULT_NOT_READY  (3): 后端未就绪
```

---

## 目录结构

```
InterRec/
├── PluginVideoRecordHook/              # 录制 DLL 核心
│   ├── PluginVideoRecordHost.cpp       #   主宿主，管理后端和命令
│   ├── PluginVideoRecordHostBackend.cpp #   后端选择与切换
│   ├── PluginVideoRecordHostFrame.cpp  #   帧处理调度
│   ├── PluginVideoRecordHostHooks.cpp  #   Hook 回调入口
│   ├── PluginVideoRecordHostState.cpp  #   状态机管理
│   ├── PluginVideoRecordVideoRecorder.cpp #   录制器主逻辑
│   ├── PluginVideoRecordVideoRecorderFrame.cpp #   帧写入逻辑
│   ├── PluginVideoRecordDx11Capture.cpp #   DX11 帧捕获
│   ├── PluginVideoRecordDx12Capture.cpp #   DX12 帧捕获
│   ├── PluginVideoRecordDx12CaptureResources.cpp #   DX12 资源管理
│   ├── PluginVideoRecordVulkanCapture.h #   Vulkan 帧捕获
│   ├── PluginVideoRecordVulkanCaptureFrame.cpp #   Vulkan 帧处理
│   ├── PluginVideoRecordVulkanCaptureResources.cpp #   Vulkan 资源管理
│   ├── PluginVideoRecordMfWriter.cpp   #   Media Foundation 编码器
│   ├── PluginVideoRecordMfQueue.cpp    #   编码队列管理
│   ├── PluginVideoRecordMfSinkWriterFactory.cpp #   SinkWriter 工厂
│   ├── PluginVideoRecordMfInterop.cpp  #   MF 互操作
│   ├── PluginVideoRecordMfAudioInterop.cpp #   MF 音频互操作
│   ├── PluginVideoRecordProcessAudioCapture.cpp #   进程音频捕获
│   ├── PluginVideoRecordAudioLoopbackInterop.cpp #   WASAPI Loopback
│   ├── PluginVideoRecordIpcServer.cpp  #   IPC 服务端
│   ├── PluginVideoRecordIpcServerSession.cpp #   IPC 会话管理
│   ├── PluginVideoRecordIpcServerState.cpp #   IPC 状态
│   ├── PluginVideoRecordPreviewPublisher.cpp #   预览发布
│   └── PluginVideoRecordHookMain.cpp   #   DLL 入口
│
├── PluginVideoRecordController/        # Win32 控制器
│
├── PluginVideoRecordVkLayer/           # Vulkan Layer 交付工程
│
├── PluginVideoRecordLoaderMono/        # Mono Loader (Unity)
│
├── PluginVideoRecordLoaderIl2Cpp/      # IL2CPP Loader
│
├── PluginVideoRecordLoaderShared/      # Loader 共用逻辑
│
├── Shared/                             # 协议与共享常量
│
└── PluginVideoRecord.sln               # VS 解决方案
```

---

## 依赖方向

```
VulkanHook
    ↑
Universal-Render-Hook
    ↑
InterRec (本仓库)
```

| 依赖 | 说明 |
|:-----|:-----|
| `Universal-Render-Hook` | `DX11 / DX12` 后端探测与回调调度 |
| `VulkanHook` | `Vulkan Layer` runtime 跟踪 |

---

## 构建

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64
```

默认产物：

```text
bin\x64\Release\PluginVideoRecordHook.dll
bin\x64\Release\PluginVideoRecordController.exe
bin\x64\Release\PluginVideoRecordVkLayer.dll
```

如果目录结构不同，可以在构建时覆盖依赖根目录：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64 /p:DependencyRoot=F:\YourWorkspace\
```

或分别覆盖：

```powershell
msbuild PluginVideoRecord.sln /p:Configuration=Release /p:Platform=x64 /p:UrhRoot=F:\Deps\Universal-Render-Hook\URH\ /p:VkhRoot=F:\Deps\VulkanHook\VulkanHook\
```

---

## 关键行为

| 行为 | 说明 |
|:-----|:-----|
| **后端探测** | 启动后同时监听 DX11 / DX12 / Vulkan，首个稳定后端被锁定用于录制 |
| **Session IPC** | 控制器通过 `SessionRegistry` 管理多个游戏进程，每个进程独立命名管道 |
| **错误保持** | 录制过程中发生错误会保持 `Error` 状态，直到手动停止 |
| **Layer Manifest** | Vulkan Layer manifest (`VkLayer_PVRC.json`) 由控制器运行时生成 |
| **自动禁用** | 缺少 `PluginVideoRecordVkLayer.dll` 时，Layer 模式开关自动禁用 |
| **QPC 同步** | 使用 `QueryPerformanceCounter` 精确计算每帧时间戳，保证音视频同步 |

---

<div align="center">

**Platform:** Windows x64 | **License:** MIT

</div>
