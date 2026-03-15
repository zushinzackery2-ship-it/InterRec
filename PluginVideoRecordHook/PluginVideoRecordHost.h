#pragma once

#include "../../RainGui/RainGui/raingui_dx11hook_types.h"
#include "../../RainGui/RainGui/raingui_dx12hook_types.h"

#include "PluginVideoRecordIpcServer.h"
#include "PluginVideoRecordPreviewPublisher.h"
#include "PluginVideoRecordUnifiedHook.h"
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
        static void OnDx11Frame(const RainGuiDx11HookRuntime* runtime, void* userData);
        static void OnDx12Frame(const RainGuiDx12HookRuntime* runtime, void* userData);

        bool InstallUnifiedHook();
        void HandleFrame(const RainGuiDx11HookRuntime* runtime);
        void HandleFrame(const RainGuiDx12HookRuntime* runtime);
        void UpdateGraphicsBackend(GraphicsBackend graphicsBackend);
        void LogMessage(const std::wstring& message);
        void ProcessPendingCommand(const RainGuiDx11HookRuntime* runtime);
        void ProcessPendingCommand(const RainGuiDx12HookRuntime* runtime);
        void PublishState(RecorderState recorderState, const std::wstring& outputPath, const std::wstring& message, LONG errorCode);

        HMODULE moduleHandle_;
        std::atomic<bool> stopRequested_;
        GraphicsBackend currentBackend_;
        bool waitingForDx12Queue_;
        PluginVideoRecordIpcServer ipcServer_;
        PluginVideoRecordPreviewPublisher previewPublisher_;
        PluginVideoRecordUnifiedHook unifiedHook_;
        PluginVideoRecordVideoRecorder recorder_;
    };
}
