#pragma once

#include "../../RainGui/RainGui/raingui_dx11hook_types.h"

#include "PluginVideoRecordIpcServer.h"
#include "PluginVideoRecordVideoRecorder.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordHost
    {
    public:
        explicit PluginVideoRecordHost(HMODULE moduleHandle);

        int Run();
        void RequestStop();

    private:
        static void OnHookSetup(const RainGuiDx11HookRuntime* runtime, void* userData);
        static void OnHookRender(const RainGuiDx11HookRuntime* runtime, void* userData);
        static void OnHookShutdown(void* userData);
        static bool IsOverlayVisible(void* userData);

        bool InstallHook();
        void HandleHookSetup(const RainGuiDx11HookRuntime* runtime);
        void HandleHookRender(const RainGuiDx11HookRuntime* runtime);
        void HandleHookShutdown();
        void ProcessPendingCommand(const RainGuiDx11HookRuntime* runtime);
        void PublishState(RecorderState recorderState, const std::wstring& outputPath, const std::wstring& message, LONG errorCode);

        HMODULE moduleHandle_;
        std::atomic<bool> stopRequested_;
        PluginVideoRecordIpcServer ipcServer_;
        PluginVideoRecordVideoRecorder recorder_;
    };
}
