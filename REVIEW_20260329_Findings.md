# InterRec Review Findings 2026-03-30

## 说明

这份文档只保留已经被源码直接证实的结论，并按“已完成修复”和“当前仍成立的事实/风险”分开记录。

---

## 已完成修复

### 1. SharedState 一致性快照

- `SharedState` 已改为 `stateSequence` 稳定快照协议。
- Hook 和 Controller 都按同一套读写规则访问状态。
- 之前那种跨字段撕裂、字符串/状态错位的问题已经按协议层修掉。

相关文件：

- `Shared/PluginVideoRecordProtocol.h`
- `PluginVideoRecordHook/PluginVideoRecordIpcServer.h`
- `PluginVideoRecordHook/PluginVideoRecordIpcServer.cpp`
- `PluginVideoRecordHook/PluginVideoRecordIpcServerState.cpp`
- `PluginVideoRecordController/PluginVideoRecordControllerIpc.cpp`

### 2. 全局单会话 IPC 冲突

- 控制面已改成 `SessionRegistry + per-session IPC`。
- 每个目标进程独立拥有自己的 `State / Preview / Log / Command`。
- Controller 会 claim 一个 live session；当前会话消失后会自动顺延接手下一个 live session。

相关文件：

- `Shared/PluginVideoRecordProtocol.h`
- `PluginVideoRecordHook/PluginVideoRecordIpcServerSession.cpp`
- `PluginVideoRecordController/PluginVideoRecordControllerIpcSession.cpp`
- `PluginVideoRecordController/PluginVideoRecordControllerIpcChannels.cpp`

### 3. 录制控制流乱接管

- Vulkan / DX11 / DX12 路径不再因为常规 runtime drift 自动停录。
- 后端临时不可用时，不再把用户选择 silently 改回默认值。
- 录制错误改成 latch 到 `Error` 状态，保留当前会话，直到用户手动点停止。

相关文件：

- `PluginVideoRecordHook/PluginVideoRecordHost.cpp`
- `PluginVideoRecordHook/PluginVideoRecordHostFrame.cpp`
- `PluginVideoRecordHook/PluginVideoRecordDx11Capture.cpp`
- `PluginVideoRecordHook/PluginVideoRecordDx12Capture.cpp`
- `PluginVideoRecordHook/PluginVideoRecordVulkanCaptureFrame.cpp`
- `PluginVideoRecordHook/PluginVideoRecordVulkanCaptureResources.cpp`
- `PluginVideoRecordController/PluginVideoRecordMainWindowState.cpp`

### 4. Vulkan 正式路径收敛

- Vulkan 正式录制路径现在只保留 Layer 模式。
- 旧的晚注入 / VEH 主业务路径已经从源码和正式产品构建里一起移除。
- `vkh_layer_exports.cpp` 仍然必须保留。
- `vkh_bootstrap.cpp` 仍然负责初始化和运行时状态复位。
- `vkh_imports.cpp` 不能直接删，但已经瘦身到 112 行，公共辅助逻辑已拆到 `vkh_imports_helpers.h`。
- `VulkanHook` 内部不再保留 `vehHandle / lateBootstrap* / BootstrapTarget*` 这些残留状态字段。

相关文件：

- `VulkanHook/VulkanHook/src/vkh_bootstrap.cpp`
- `VulkanHook/VulkanHook/src/vkh_imports.cpp`
- `VulkanHook/VulkanHook/src/vkh_imports_helpers.h`
- `VulkanHook/VulkanHook/src/vkh_layer_exports.cpp`
- `VulkanHook/VulkanHook/src/vkh_internal.h`

### 5. 依赖方向收敛

- `Universal-Render-Hook` 已切成 headless hook 核心，不再直接依赖 `RainGui`。
- `InterRec` 的 `Hook / VkLayer` 产物也不再把 `RainGui` 源码和 `raingui_impl_*` 编进来。

相关文件：

- `../Universal-Render-Hook/URH/src/urh_dx11_internal.h`
- `../Universal-Render-Hook/URH/src/urh_dx12_internal.h`
- `PluginVideoRecordHook/PluginVideoRecordHook.vcxproj`
- `PluginVideoRecordVkLayer/PluginVideoRecordVkLayer.vcxproj`
### 6. 编码队列策略

- `PluginVideoRecordMfWriter` 已从固定小队列改为弹性内存队列。
- 队列会根据当前样本大小和可用物理内存动态扩容。
- 队列压力退回后会自动收缩预算。
- 真正顶到预算和可用内存上限时才会丢样本，并记录 drop 日志。
- 旧的“音频积压直接报错停录”逻辑已经移除。

相关文件：

- `PluginVideoRecordHook/PluginVideoRecordMfWriter.cpp`
- `PluginVideoRecordHook/PluginVideoRecordMfQueue.cpp`
- `PluginVideoRecordHook/PluginVideoRecordMfQueueInternal.h`

### 7. 结构拆分

以下超大文件已完成第一轮解耦：

- `PluginVideoRecordHook/PluginVideoRecordIpcServer.cpp`
  - 拆为 `IpcServer.cpp / IpcServerState.cpp / IpcServerSession.cpp`
- `PluginVideoRecordController/PluginVideoRecordControllerIpc.cpp`
  - 拆为 `ControllerIpc.cpp / ControllerIpcSession.cpp / ControllerIpcChannels.cpp`
- `PluginVideoRecordHook/PluginVideoRecordHost.cpp`
  - 拆为 `Host.cpp / HostHooks.cpp / HostBackend.cpp / HostState.cpp`

并新增了共用锁封装：

- `Shared/PluginVideoRecordScopedMutexLock.h`

### 8. 构建验证

已用 `InterRec\\build.bat` 完整验证：

- `PluginVideoRecordHook.dll`
- `PluginVideoRecordVkLayer.dll`
- `PluginVideoRecordController.exe`

验证时间：

- 2026-03-30

---

## 当前仍成立的事实 / 风险

### 1. Vulkan 捕获仍然发生在真正 present 之前

这仍然是当前 Vulkan 路径的结构事实：

- Hook / Layer 都是在真实 `vkQueuePresentKHR` 之前进入抓帧逻辑。
- 复制命令仍然提交到游戏当前 present queue 上。

这意味着 Vulkan 路径即使已经把明显 CPU 卡顿压下去了，图形队列上仍然天然存在额外串行压力。

相关文件：

- `VulkanHook/VulkanHook/src/vkh_hooks.cpp`
- `VulkanHook/VulkanHook/src/vkh_layer_exports.cpp`
- `PluginVideoRecordHook/PluginVideoRecordVulkanCaptureFrame.cpp`

### 2. Vulkan 读回资源模块仍然偏大

`PluginVideoRecordVulkanCaptureResources.cpp` 仍是当前最明显的后续重构热点。

现状：

- 文件体量大
- 资源创建、重绑、内存选择、队列族推断、销毁逻辑仍耦合在一起

结论：

- 这里已经不是业务正确性阻塞点，但仍是可维护性风险点

### 3. 弹性队列不是无限队列

现在的行为已经是：

- 优先扩容
- 压力解除后回缩
- 只有在预算上限和可用物理内存都不允许时才 drop

但它依然不是无限缓存。

如果编码线程长期跟不上，或者系统可用内存持续偏低，最终仍会发生丢帧/丢音频包，只是现在这个行为是可观测、可解释、不会直接接管停录控制流的。

### 4. 自动化 smoketest 仍偏轻

`run_vulkan_record_smoketest.py` 已随 session 化协议更新，但这轮没有把它升级成严格失败即退出的完整回归工具。

结论：

- 当前主验证手段仍然是 `build.bat + 实机日志/手测`

---

## 当前最强结论

截至 2026-03-30，已经可以明确确认：

1. 当前正式 Vulkan 录制路径是 Layer-only。
2. 多进程控制面冲突已经按协议层解决。
3. 录制状态不再被常规 drift 或临时错误自动接管停止。
4. 编码队列已改成弹性内存预算模型，不再是固定小队列。
5. 项目结构已经完成一轮针对 IPC / Host / Writer 的拆分，代码健康度明显好于前两轮。
