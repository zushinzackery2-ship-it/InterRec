#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace PluginVideoRecord
{
    PluginVideoRecordHost::PluginVideoRecordHost(HMODULE moduleHandle)
        : moduleHandle_(moduleHandle)
        , stopRequested_(false)
        , currentBackend_(GraphicsBackend::Unknown)
        , waitingForDx12Queue_(false)
    {
        recorder_.SetPreviewPublisher(&previewPublisher_);
    }

    int PluginVideoRecordHost::Run()
    {
        if (!ipcServer_.Start())
        {
            return 1;
        }

        previewPublisher_.Start();
        PublishState(RecorderState::Standby, L"", L"", 0);
        LogMessage(L"宿主已启动，等待图形 Hook。");

        while (!stopRequested_.load())
        {
            if (InstallUnifiedHook())
            {
                LogMessage(L"统一图形 Hook 安装成功。");
                break;
            }

            Sleep(500);
        }

        while (!stopRequested_.load())
        {
            Sleep(250);
        }

        recorder_.Stop();
        unifiedHook_.Uninstall();
        ipcServer_.SetGraphicsBackend(GraphicsBackend::Unknown);
        previewPublisher_.Stop();
        ipcServer_.Stop();
        return 0;
    }

    void PluginVideoRecordHost::RequestStop()
    {
        stopRequested_.store(true);
    }

    void PluginVideoRecordHost::OnDx11Frame(const RainGuiDx11HookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleFrame(runtime);
        }
    }

    void PluginVideoRecordHost::OnDx12Frame(const RainGuiDx12HookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleFrame(runtime);
        }
    }

    bool PluginVideoRecordHost::InstallUnifiedHook()
    {
        PluginVideoRecordUnifiedHook::Callbacks callbacks = {};
        callbacks.onDx11Frame = OnDx11Frame;
        callbacks.onDx12Frame = OnDx12Frame;
        callbacks.userData = this;

        std::wstring error;
        return unifiedHook_.Install(callbacks, error);
    }

    void PluginVideoRecordHost::HandleFrame(const RainGuiDx11HookRuntime* runtime)
    {
        UpdateGraphicsBackend(GraphicsBackend::Dx11);
        ProcessPendingCommand(runtime);

        std::wstring error;
        if (recorder_.IsRecording() && !recorder_.OnFrame(runtime, error))
        {
            const std::wstring outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            LogMessage(L"DX11 录制已停止：" + error);
            PublishState(RecorderState::Error, outputPath, error, -1);
        }
    }

    void PluginVideoRecordHost::HandleFrame(const RainGuiDx12HookRuntime* runtime)
    {
        UpdateGraphicsBackend(GraphicsBackend::Dx12);
        if (runtime && runtime->commandQueue && waitingForDx12Queue_)
        {
            waitingForDx12Queue_ = false;
            LogMessage(L"DX12 命令队列已就绪。");
        }

        ProcessPendingCommand(runtime);

        std::wstring error;
        if (recorder_.IsRecording() && !recorder_.OnFrame(runtime, error))
        {
            const std::wstring outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            LogMessage(L"DX12 录制已停止：" + error);
            PublishState(RecorderState::Error, outputPath, error, -1);
        }
    }

    void PluginVideoRecordHost::UpdateGraphicsBackend(GraphicsBackend graphicsBackend)
    {
        if (currentBackend_ == graphicsBackend)
        {
            return;
        }

        currentBackend_ = graphicsBackend;
        ipcServer_.SetGraphicsBackend(graphicsBackend);

        switch (graphicsBackend)
        {
        case GraphicsBackend::Dx11:
            LogMessage(L"已识别图形后端：DX11。");
            break;

        case GraphicsBackend::Dx12:
            LogMessage(L"已识别图形后端：DX12。");
            break;

        default:
            break;
        }
    }

    void PluginVideoRecordHost::LogMessage(const std::wstring& message)
    {
        ipcServer_.AppendLog(message);
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const RainGuiDx11HookRuntime* runtime)
    {
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
            LogMessage(L"收到开始录制命令（DX11）。");
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                LogMessage(L"DX11 开始录制成功：" + outputPath);
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                LogMessage(L"DX11 开始录制失败：" + error);
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const RainGuiDx12HookRuntime* runtime)
    {
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
            if (!runtime || !runtime->commandQueue)
            {
                if (!waitingForDx12Queue_)
                {
                    waitingForDx12Queue_ = true;
                    LogMessage(L"DX12 命令队列尚未捕获，开始录制命令延后处理。");
                }
                return;
            }

            LogMessage(L"收到开始录制命令（DX12）。");
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                LogMessage(L"DX12 开始录制成功：" + outputPath);
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                LogMessage(L"DX12 开始录制失败：" + error);
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            outputPath = recorder_.GetCurrentOutputPath();
            recorder_.Stop();
            waitingForDx12Queue_ = false;
            LogMessage(L"收到停止录制命令。");
            PublishState(RecorderState::Standby, outputPath, L"", 0);
            break;

        default:
            break;
        }

        ipcServer_.AcknowledgeCommand(commandType, sequence);
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
