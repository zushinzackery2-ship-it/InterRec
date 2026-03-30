InterRec 打包说明

runtime:
- PluginVideoRecordHook.dll
- PluginVideoRecordController.exe
- PluginVideoRecordVkLayer.dll
- PluginVideoRecordLoaderMono.dll（如果当前环境已成功构建）

symbols:
- PluginVideoRecordHook.pdb
- PluginVideoRecordController.pdb

使用方式：
1. 将 PluginVideoRecordHook.dll 和对应 Loader 放到目标游戏的 BepInEx\plugins 目录。
2. Vulkan 游戏需要把 `PluginVideoRecordVkLayer.dll` 与控制器放在同一目录；控制器会在运行时自动生成 Layer manifest，并在启用 `Vulkan-Layer模式` 后于目标进程重启时生效。
3. 启动游戏后运行 PluginVideoRecordController.exe。
4. 录制文件默认输出到游戏根目录的 PluginVideoRecord 文件夹。

说明：
- 当前 Hook 端已切到 RainGui AutoHook，会自动锁定稳定的 DX11 / DX12 后端。
- 控制器会按 `PluginVideoRecordVkLayer.dll` 的实际路径生成运行时 manifest，不再依赖静态 json 交付文件。
