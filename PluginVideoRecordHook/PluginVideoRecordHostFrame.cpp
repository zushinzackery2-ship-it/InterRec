#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace PluginVideoRecord
{
    void PluginVideoRecordHost::HandleFrame(const UrhDx11HookRuntime* runtime)
    {
        if (!runtime)
        {
            return;
        }

        UpdateBackendAvailability(GraphicsBackend::Dx11, true);
        ProcessPendingCommand(runtime);

        if (recordingFaulted_)
        {
            return;
        }

        std::wstring error;
        if (recorder_.IsRecording() &&
            GetActiveRecordingBackend() == GraphicsBackend::Dx11 &&
            !recorder_.OnFrame(runtime, error))
        {
            const std::wstring outputPath = recorder_.GetCurrentOutputPath();
            recordingFaulted_ = true;
            LogMessage(L"DX11 录制发生错误：" + error);
            PublishState(RecorderState::Error, outputPath, error, -1);
        }
    }

    void PluginVideoRecordHost::HandleFrame(const UrhDx12HookRuntime* runtime)
    {
        if (!runtime)
        {
            return;
        }

        UpdateBackendAvailability(GraphicsBackend::Dx12, true);
        ProcessPendingCommand(runtime);

        if (recordingFaulted_)
        {
            return;
        }

        std::wstring error;
        if (recorder_.IsRecording() &&
            GetActiveRecordingBackend() == GraphicsBackend::Dx12 &&
            !recorder_.OnFrame(runtime, error))
        {
            const std::wstring outputPath = recorder_.GetCurrentOutputPath();
            recordingFaulted_ = true;
            LogMessage(L"DX12 录制发生错误：" + error);
            PublishState(RecorderState::Error, outputPath, error, -1);
        }
    }

    void PluginVideoRecordHost::HandleFrame(const UrhVulkanHookRuntime* runtime)
    {
        if (!IsVulkanRuntimeUsable(runtime))
        {
            return;
        }

        UpdateBackendAvailability(GraphicsBackend::Vulkan, true);
        ProcessPendingCommand(runtime);

        if (recordingFaulted_)
        {
            return;
        }

        std::wstring error;
        if (recorder_.IsRecording() &&
            GetActiveRecordingBackend() == GraphicsBackend::Vulkan &&
            !recorder_.OnFrame(runtime, error))
        {
            const std::wstring outputPath = recorder_.GetCurrentOutputPath();
            recordingFaulted_ = true;
            LogMessage(L"Vulkan 录制发生错误：" + error);
            PublishState(RecorderState::Error, outputPath, error, -1);
        }
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const UrhDx11HookRuntime* runtime)
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);

        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryPeekCommand(commandType, sequence))
        {
            return;
        }

        std::wstring outputPath;
        std::wstring error;

        switch (commandType)
        {
        case CommandType::StartRecording:
            if (GetSelectedBackend() != GraphicsBackend::Dx11)
            {
                return;
            }

            LogMessage(L"收到开始录制命令（DX11）。");
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                SetActiveRecordingBackend(GraphicsBackend::Dx11);
                PublishState(recordingFaulted_ ? RecorderState::Error : RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                recordingFaulted_ = false;
                LogMessage(L"DX11 开始录制成功：" + outputPath);
                SetActiveRecordingBackend(GraphicsBackend::Dx11);
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                LogMessage(L"DX11 开始录制失败：" + error);
                SetActiveRecordingBackend(GraphicsBackend::Unknown);
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            recordingFaulted_ = false;
            SetActiveRecordingBackend(GraphicsBackend::Unknown);
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const UrhDx12HookRuntime* runtime)
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);

        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryPeekCommand(commandType, sequence))
        {
            return;
        }

        std::wstring outputPath;
        std::wstring error;

        switch (commandType)
        {
        case CommandType::StartRecording:
            if (GetSelectedBackend() != GraphicsBackend::Dx12)
            {
                return;
            }

            LogMessage(L"收到开始录制命令（DX12）。");
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                SetActiveRecordingBackend(GraphicsBackend::Dx12);
                PublishState(recordingFaulted_ ? RecorderState::Error : RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                recordingFaulted_ = false;
                LogMessage(L"DX12 开始录制成功：" + outputPath);
                SetActiveRecordingBackend(GraphicsBackend::Dx12);
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                LogMessage(L"DX12 开始录制失败：" + error);
                SetActiveRecordingBackend(GraphicsBackend::Unknown);
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            recordingFaulted_ = false;
            SetActiveRecordingBackend(GraphicsBackend::Unknown);
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const UrhVulkanHookRuntime* runtime)
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);

        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryPeekCommand(commandType, sequence))
        {
            return;
        }

        std::wstring outputPath;
        std::wstring error;

        switch (commandType)
        {
        case CommandType::StartRecording:
            if (GetSelectedBackend() != GraphicsBackend::Vulkan)
            {
                return;
            }

            if (!IsVulkanRuntimeUsable(runtime))
            {
                if (!waitingForVulkanRuntime_)
                {
                    waitingForVulkanRuntime_ = true;
                    LogMessage(L"Vulkan 运行时尚未完全就绪，开始录制命令延后处理。");
                }
                return;
            }

            waitingForVulkanRuntime_ = false;
            LogMessage(L"收到开始录制命令（Vulkan）。");
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                SetActiveRecordingBackend(GraphicsBackend::Vulkan);
                PublishState(recordingFaulted_ ? RecorderState::Error : RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                LogMessage(L"Vulkan 开始录制成功：" + outputPath);
                recordingFaulted_ = false;
                SetActiveRecordingBackend(GraphicsBackend::Vulkan);
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                LogMessage(L"Vulkan 开始录制失败：" + error);
                SetActiveRecordingBackend(GraphicsBackend::Unknown);
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            waitingForVulkanRuntime_ = false;
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            recordingFaulted_ = false;
            SetActiveRecordingBackend(GraphicsBackend::Unknown);
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
    }

    void PluginVideoRecordHost::ProcessPendingUnknownCommand(const std::wstring& message)
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);

        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryPeekCommand(commandType, sequence))
        {
            return;
        }

        std::wstring outputPath;

        switch (commandType)
        {
        case CommandType::StartRecording:
            recorder_.Stop();
            recordingFaulted_ = false;
            SetActiveRecordingBackend(GraphicsBackend::Unknown);
            outputPath = recorder_.GetPendingOutputPath();
            if (!message.empty())
            {
                LogMessage(message);
            }
            PublishState(RecorderState::Error, outputPath, message, -1);
            break;

        case CommandType::StopRecording:
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            recordingFaulted_ = false;
            SetActiveRecordingBackend(GraphicsBackend::Unknown);
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
    }
}
