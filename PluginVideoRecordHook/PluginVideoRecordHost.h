#pragma once

#include <urh/urh.h>
#include <urh/dx11_types.h>
#include <urh/dx12_types.h>
#include <urh/vulkan_types.h>

#include "PluginVideoRecordIpcServer.h"
#include "PluginVideoRecordPreviewPublisher.h"
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
        static void OnAutoFrame(const UrhAutoHookRuntime* runtime, void* userData);
        static void OnVulkanFrame(const UrhVulkanHookRuntime* runtime, void* userData);

        bool InstallAutoHook();
        bool InstallVulkanHook();
        bool InstallDxHooks();
        bool InstallLayerModeHooks();
        void ShutdownHooks();
        bool IsVulkanRuntimeReady() const;
        static bool IsVulkanRuntimeUsable(const UrhVulkanHookRuntime* runtime);
        void HandleFrame(const UrhAutoHookRuntime* runtime);
        void HandleFrame(const UrhDx11HookRuntime* runtime);
        void HandleFrame(const UrhDx12HookRuntime* runtime);
        void HandleFrame(const UrhVulkanHookRuntime* runtime);
        void PollBackendDiagnostics();
        void PollPendingRuntimeCommand();
        void UpdateBackendAvailability(GraphicsBackend graphicsBackend, bool available);
        void EnsureSelectedBackend();
        GraphicsBackend GetSelectedBackend() const;
        GraphicsBackend GetActiveRecordingBackend() const;
        void SetActiveRecordingBackend(GraphicsBackend graphicsBackend);
        void LogMessage(const std::wstring& message);
        void ProcessPendingCommand(const UrhDx11HookRuntime* runtime);
        void ProcessPendingCommand(const UrhDx12HookRuntime* runtime);
        void ProcessPendingCommand(const UrhVulkanHookRuntime* runtime);
        void ProcessPendingUnknownCommand(const std::wstring& message);
        void PublishState(RecorderState recorderState, const std::wstring& outputPath, const std::wstring& message, LONG errorCode);

        HMODULE moduleHandle_;
        std::atomic<bool> stopRequested_;
        LONG availableBackendMask_;
        GraphicsBackend selectedBackend_;
        GraphicsBackend activeRecordingBackend_;
        GraphicsBackend lastAutoHookBackend_;
        bool autoHookInstalled_;
        bool vulkanHookInstalled_;
        bool vulkanLayerModule_;
        bool waitingForVulkanRuntime_;
        bool recordingFaulted_;
        bool lastVulkanRecognizedState_;
        bool lastVulkanReadyState_;
        bool lastVulkanRuntimeState_;
        std::mutex commandMutex_;
        PluginVideoRecordIpcServer ipcServer_;
        PluginVideoRecordPreviewPublisher previewPublisher_;
        PluginVideoRecordVideoRecorder recorder_;
    };
}
