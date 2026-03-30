<div align="center">

# InterRec

**Windows 游戏进程内录制工具**

*DX11 / DX12 / Vulkan-Layer | Sessionized IPC | Media Foundation*

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

## 目录

```text
PluginVideoRecordHook/           录制 DLL
PluginVideoRecordController/     Win32 控制器
PluginVideoRecordVkLayer/        Vulkan Layer 交付工程
PluginVideoRecordLoaderMono/     Mono Loader
PluginVideoRecordLoaderIl2Cpp/   IL2CPP Loader（源码）
PluginVideoRecordLoaderShared/   Loader 共用逻辑
Shared/                          协议与共享常量
```

## 依赖

| 依赖 | 说明 |
|:-----|:-----|
| `Universal-Render-Hook` | `DX11 / DX12` 后端探测与回调调度 |
| `VulkanHook` | `Vulkan Layer` runtime 跟踪 |
| `RainGui` | 不属于正式录制产物依赖 |

如果目录结构不同，可以在构建时覆盖依赖根目录：

```powershell
.\build.bat /p:DependencyRoot=F:\YourWorkspace\
```

或分别覆盖：

```powershell
.\build.bat /p:UrhRoot=F:\Deps\Universal-Render-Hook\URH\ /p:VkhRoot=F:\Deps\VulkanHook\VulkanHook\
```

## 构建

```powershell
.\build.bat
```

默认产物：

```text
bin\x64\Release\PluginVideoRecordHook.dll
bin\x64\Release\PluginVideoRecordController.exe
bin\x64\Release\PluginVideoRecordVkLayer.dll
```

## 关键行为

- 控制面使用 `SessionRegistry + per-session IPC`
- 录制错误会保持在 `Error`，直到手动停止
- Vulkan Layer manifest 由控制器运行时生成
- 缺少 `PluginVideoRecordVkLayer.dll` 时，Layer 模式开关自动禁用

## 许可

MIT，见根目录 `LICENSE`
