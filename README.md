<div align="center">

# InterRec

游戏进程内录制工具，基于 RainGui 图形 Hook，支持 DX11 / DX12

<!-- 在这里放一张效果截图或 GIF -->
<!-- ![InterRec Demo](docs/demo.gif) -->

</div>

> [!NOTE]
> 专注游戏进程内抓帧 + 进程独占音频录制，通过独立 IPC 控制器操作

> [!IMPORTANT]
> **自动识别 DX11 / DX12** — 基于 RainGui 统一图形 Hook，运行时自动选择后端
>
> **独立控制器** — Win32 原生 IPC 控制，录制 / 预览 / 日志三页面
>
> **进程音频隔离** — 只录目标进程的声音，不录系统全局

## 功能

- 运行时自动识别 DX11 / DX12 并安装对应 Hook
- 独立 Win32 控制器通过 IPC 控制录制
- 控制器实时显示通信状态、图形后端、录制状态、输出路径
- 预览页显示实时缩略画面，日志页显示 Hook 端和控制器日志
- 进程音频隔离录制
- H.264 编码，目标 120 FPS / 12 Mbps
- DX12 支持 RGBA / BGRA / 10-bit / FP16 后缓冲格式
- 分辨率跟随游戏后缓冲，自动修正为偶数适配编码器
- BepInEx 插件自动加载原生录制 DLL
- 输出路径：`\PluginVideoRecord\yyyyMMdd_HHmmss.mp4`

## 工程结构

```
PluginVideoRecordHook/           原生录制 DLL（Hook + 采集 + 编码 + IPC）
PluginVideoRecordController/     独立 Win32 控制器
PluginVideoRecordLoaderMono/     BepInEx 5 / Mono Loader
PluginVideoRecordLoaderIl2Cpp/   BepInEx 6 / IL2CPP Loader（源码）
PluginVideoRecordLoaderShared/   Loader 共用加载逻辑
Shared/                          IPC 协议和共享常量
```

## 依赖

- **RainGui** — 源码级引用，默认要求与本仓库同级放置

```
InterRec/
../RainGui/
```

- **BepInEx** — Mono 游戏需要 BepInEx 5，IL2CPP 游戏需要 BepInEx 6

## 构建

```powershell
.\build.bat
```

产物：

```
bin\x64\Release\PluginVideoRecordHook.dll
bin\x64\Release\PluginVideoRecordController.exe
bin\x64\Release\PluginVideoRecordLoaderMono.dll
```

## 使用

1. 将 Loader DLL + `PluginVideoRecordHook.dll` 放到 `BepInEx\plugins` 子目录
2. 启动游戏
3. 运行 `PluginVideoRecordController.exe`
4. 在控制器 `录制` 页操作开始 / 结束
5. 录制文件写入游戏目录 `PluginVideoRecord\` 文件夹