# InterRec

游戏进程内录制工具，支持 `DX11 / DX12 / Vulkan`。

## 定位

- `DX11 / DX12` 走 `Universal-Render-Hook`
- `Vulkan` 走标准 `Layer` 链
- 录制业务包含抓帧、音频、编码、IPC、控制器
- 当前正式 Vulkan 路径只保留 `Layer` 模式

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

- `RainGui` 不再直接编进正式录制产物
- Mono Loader 需要本地 BepInEx / Unity 引用路径
- `Universal-Render-Hook` / `VulkanHook` 默认按当前同级目录查找
- 如果目录结构不同，可以在构建时覆盖：

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

## 使用

1. `DX11 / DX12`：把 Loader 和 `PluginVideoRecordHook.dll` 放进 `BepInEx\plugins`
2. `Vulkan`：控制器里启用 `Vulkan-Layer模式`，然后重启目标进程
3. 运行 `PluginVideoRecordController.exe`
4. 录制文件输出到游戏目录下 `PluginVideoRecord\yyyyMMdd_HHmmss.mp4`

## 关键行为

- 控制面是 `SessionRegistry + per-session IPC`
- 录制错误会保持在 `Error`，直到手动停止
- Vulkan Layer manifest 由控制器运行时生成
- 缺少 `PluginVideoRecordVkLayer.dll` 时，Layer 模式开关自动禁用

## 许可

MIT，见根目录 `LICENSE`
