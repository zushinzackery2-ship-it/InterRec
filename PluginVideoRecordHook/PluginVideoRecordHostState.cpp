#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace PluginVideoRecord
{
    void PluginVideoRecordHost::HandleFrame(const UrhAutoHookRuntime* runtime)
    {
        if (!runtime)
        {
            return;
        }

        switch (runtime->backend)
        {
        case UrhAutoHookBackend_Dx11:
            HandleFrame(static_cast<const UrhDx11HookRuntime*>(runtime->nativeRuntime));
            return;

        case UrhAutoHookBackend_Dx12:
            HandleFrame(static_cast<const UrhDx12HookRuntime*>(runtime->nativeRuntime));
            return;

        default:
            break;
        }
    }

    void PluginVideoRecordHost::PollPendingRuntimeCommand()
    {
        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryPeekCommand(commandType, sequence))
        {
            return;
        }

        GraphicsBackend targetBackend = GetSelectedBackend();
        if (commandType == CommandType::StopRecording && recorder_.IsRecording())
        {
            targetBackend = GetActiveRecordingBackend();
        }

        switch (targetBackend)
        {
        case GraphicsBackend::Dx11:
        case GraphicsBackend::Dx12:
            if (autoHookInstalled_)
            {
                const URH::Runtime* runtime = URH::GetRuntime();
                if (runtime && runtime->nativeRuntime)
                {
                    if (targetBackend == GraphicsBackend::Dx11 &&
                        runtime->backend == UrhAutoHookBackend_Dx11)
                    {
                        ProcessPendingCommand(static_cast<const UrhDx11HookRuntime*>(runtime->nativeRuntime));
                        return;
                    }

                    if (targetBackend == GraphicsBackend::Dx12 &&
                        runtime->backend == UrhAutoHookBackend_Dx12)
                    {
                        ProcessPendingCommand(static_cast<const UrhDx12HookRuntime*>(runtime->nativeRuntime));
                        return;
                    }
                }
            }

            if (commandType == CommandType::StartRecording)
            {
                ProcessPendingUnknownCommand(L"当前 DX AutoHook 尚未命中所选后端。");
            }
            else
            {
                ProcessPendingUnknownCommand(L"");
            }
            return;

        case GraphicsBackend::Vulkan:
            if (!vulkanHookInstalled_)
            {
                ProcessPendingUnknownCommand(L"当前进程未启用 Vulkan-Layer模式，不能使用 Vulkan 录制。");
                return;
            }

            if (const URH::VulkanRuntime* runtime = URH::GetVulkanRuntime())
            {
                ProcessPendingCommand(runtime);
                return;
            }

            if (commandType == CommandType::StartRecording)
            {
                ProcessPendingUnknownCommand(L"当前 Vulkan Hook 还没有可用运行时。");
            }
            else
            {
                ProcessPendingUnknownCommand(L"");
            }
            return;

        default:
            break;
        }

        ProcessPendingUnknownCommand(L"当前没有可用的录制后端。");
    }

    GraphicsBackend PluginVideoRecordHost::GetSelectedBackend() const
    {
        return selectedBackend_;
    }

    GraphicsBackend PluginVideoRecordHost::GetActiveRecordingBackend() const
    {
        return activeRecordingBackend_;
    }

    void PluginVideoRecordHost::SetActiveRecordingBackend(GraphicsBackend graphicsBackend)
    {
        activeRecordingBackend_ = graphicsBackend;
        ipcServer_.SetActiveRecordingBackend(graphicsBackend);
    }

    void PluginVideoRecordHost::LogMessage(const std::wstring& message)
    {
        ipcServer_.AppendLog(message);
    }

    void PluginVideoRecordHost::PublishState(
        RecorderState recorderState,
        const std::wstring& outputPath,
        const std::wstring& message,
        LONG errorCode)
    {
        std::wstring stateOutputPath = outputPath;
        if (stateOutputPath.empty())
        {
            stateOutputPath = recorder_.GetPendingOutputPath();
        }

        ipcServer_.SetRecorderState(recorderState, stateOutputPath, message, errorCode);
    }
}
