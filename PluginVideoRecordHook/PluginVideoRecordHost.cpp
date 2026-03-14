#include "pch.h"

#include "../../RainGui/RainGui/raingui_impl_dx11hook.h"

#include "PluginVideoRecordHost.h"

namespace PluginVideoRecord
{
    PluginVideoRecordHost::PluginVideoRecordHost(HMODULE moduleHandle)
        : moduleHandle_(moduleHandle)
        , stopRequested_(false)
    {
    }

    int PluginVideoRecordHost::Run()
    {
        if (!ipcServer_.Start())
        {
            return 1;
        }

        PublishState(RecorderState::Standby, L"", L"", 0);

        while (!stopRequested_.load())
        {
            if (RainGuiDx11Hook::IsInstalled() || InstallHook())
            {
                break;
            }

            Sleep(500);
        }

        while (!stopRequested_.load())
        {
            Sleep(250);
        }

        recorder_.Stop();

        if (RainGuiDx11Hook::IsInstalled())
        {
            RainGuiDx11Hook::Shutdown();
        }

        ipcServer_.Stop();
        return 0;
    }

    void PluginVideoRecordHost::RequestStop()
    {
        stopRequested_.store(true);
    }

    void PluginVideoRecordHost::OnHookSetup(const RainGuiDx11HookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleHookSetup(runtime);
        }
    }

    void PluginVideoRecordHost::OnHookRender(const RainGuiDx11HookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleHookRender(runtime);
        }
    }

    void PluginVideoRecordHost::OnHookShutdown(void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleHookShutdown();
        }
    }

    bool PluginVideoRecordHost::IsOverlayVisible(void*)
    {
        return false;
    }

    bool PluginVideoRecordHost::InstallHook()
    {
        RainGuiDx11HookDesc description = {};
        RainGuiDx11Hook::FillDefaultDesc(&description);
        description.onSetup = OnHookSetup;
        description.onRender = OnHookRender;
        description.onShutdown = OnHookShutdown;
        description.isVisible = IsOverlayVisible;
        description.userData = this;
        description.hookWndProc = false;
        description.blockInputWhenVisible = false;
        description.enableDefaultDebugWindow = false;
        description.startVisible = false;
        description.toggleVirtualKey = 0;
        description.warmupFrames = 10;
        return RainGuiDx11Hook::Init(&description);
    }

    void PluginVideoRecordHost::HandleHookSetup(const RainGuiDx11HookRuntime*)
    {
        PublishState(RecorderState::Standby, L"", L"", 0);
    }

    void PluginVideoRecordHost::HandleHookRender(const RainGuiDx11HookRuntime* runtime)
    {
        ProcessPendingCommand(runtime);

        std::wstring error;
        if (recorder_.IsRecording() && !recorder_.OnFrame(runtime, error))
        {
            recorder_.Stop();
            PublishState(RecorderState::Error, L"", error, -1);
        }
    }

    void PluginVideoRecordHost::HandleHookShutdown()
    {
        recorder_.Stop();
        PublishState(RecorderState::Standby, L"", L"", 0);
    }

    void PluginVideoRecordHost::ProcessPendingCommand(const RainGuiDx11HookRuntime* runtime)
    {
        CommandType commandType = CommandType::None;
        LONG sequence = 0;
        if (!ipcServer_.TryDequeueCommand(commandType, sequence))
        {
            return;
        }

        std::wstring outputPath;
        std::wstring error;

        switch (commandType)
        {
        case CommandType::StartRecording:
            if (recorder_.IsRecording())
            {
                outputPath = recorder_.GetCurrentOutputPath();
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else if (recorder_.Start(runtime, outputPath, error))
            {
                PublishState(RecorderState::Recording, outputPath, L"", 0);
            }
            else
            {
                PublishState(RecorderState::Error, L"", error, -1);
            }
            break;

        case CommandType::StopRecording:
            recorder_.Stop();
            PublishState(RecorderState::Standby, L"", L"", 0);
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
        ipcServer_.SetRecorderState(recorderState, outputPath, message, errorCode);
    }
}
