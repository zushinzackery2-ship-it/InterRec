# InterRec

`InterRec` 是一个自用的 DX11 游戏内录制工具，用于游戏进程内录制。

## 主要功能

- 基于 `RainGui` 的 `DX11 Present Hook` 抓取游戏画面
- 输出 `120 FPS` 视频
- 只录制目标游戏进程自己的声音，不录系统全局声音
- `Win32` 独立控制器可通过 `IPC` 控制开始录制和结束录制
- 控制器实时显示：
  - 通信状态：`正常 / 离线`
  - 录制状态：`录制中 / 待机中`
- `BepInEx` 插件负责 `LoadLibrary` 加载原生录制 DLL
- 输出文件路径：
  - `\\PluginVideoRecord\\yyyyMMdd_HHmmss.mp4`

## 工程结构

- `PluginVideoRecordHook/`
  - 原生录制 DLL
  - 负责 DX11 Hook、视频采集、进程音频采集、MP4 混流、IPC 服务
- `PluginVideoRecordController/`
  - 独立 Win32 控制器
- `PluginVideoRecordLoader/`
  - BepInEx Loader 插件
- `Shared/`
  - IPC 协议和共享常量

## 依赖

- `RainGui`
  - 远程地址：`https://github.com/zushinzackery2-ship-it/RainGui.git`
  - 当前工程按源码方式直接引用，默认要求 `RainGui` 仓库与本仓库同级放置
  - 预期本地目录：

```text
InterRec/
..\RainGui/
```

- `BepInEx`
  - `PluginVideoRecordLoader` 需要目标游戏环境中已有可用的 `BepInEx 5`

## 构建

项目默认使用 `VS2022 x64 Release`，构建前会先调用开发者命令提示符。

```powershell
cd .\PluginVideoRecord
.\build.bat
```

说明：

- `PluginVideoRecordHook` 会直接编译 `..\RainGui\RainGui\` 下的源码文件
- 如果 `RainGui` 不在默认相对路径，需要先调整工程里的包含路径和源码引用

构建产物位于：

```text
bin\x64\Release\PluginVideoRecordHook.dll
bin\x64\Release\PluginVideoRecordController.exe
bin\x64\Release\PluginVideoRecordLoader.dll
```

## 使用方式

1. 将 `PluginVideoRecordLoader.dll` 和 `PluginVideoRecordHook.dll` 放到同一个 `BepInEx\plugins` 子目录。
2. 启动游戏，让 BepInEx 自动加载 `PluginVideoRecordLoader.dll`。
3. 运行 `PluginVideoRecordController.exe`。
4. 在控制器里点击“开始录制”或“结束录制”。
5. 录制文件会写到游戏目录下的 `PluginVideoRecord` 文件夹。

## 当前说明

- 当前实现目标是自用录制链路打通，不追求做成通用录屏软件。
- 录制分辨率跟随游戏当前 DX11 back buffer。
- 发生分辨率或后缓冲格式变化时，当前录制会停止并等待重新开始。
