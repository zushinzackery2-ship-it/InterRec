#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace
{
    constexpr wchar_t VulkanLayerModuleName[] = L"PluginVideoRecordVkLayer.dll";

    bool IsCurrentModuleVulkanLayer(HMODULE moduleHandle)
    {
        if (!moduleHandle)
        {
            return false;
        }

        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(moduleHandle, modulePath, static_cast<DWORD>(_countof(modulePath)));
        if (length == 0 || length >= _countof(modulePath))
        {
            return false;
        }

        const wchar_t* fileName = wcsrchr(modulePath, L'\\');
        fileName = fileName ? fileName + 1 : modulePath;
        return _wcsicmp(fileName, VulkanLayerModuleName) == 0;
    }

}

namespace PluginVideoRecord
{
    PluginVideoRecordHost::PluginVideoRecordHost(HMODULE moduleHandle)
        : moduleHandle_(moduleHandle)
        , stopRequested_(false)
        , availableBackendMask_(GraphicsBackendMask_None)
        , selectedBackend_(GraphicsBackend::Unknown)
        , activeRecordingBackend_(GraphicsBackend::Unknown)
        , lastAutoHookBackend_(GraphicsBackend::Unknown)
        , autoHookInstalled_(false)
        , vulkanHookInstalled_(false)
        , vulkanLayerModule_(false)
        , waitingForVulkanRuntime_(false)
        , recordingFaulted_(false)
        , lastVulkanRecognizedState_(false)
        , lastVulkanReadyState_(false)
        , lastVulkanRuntimeState_(false)
    {
        recorder_.SetPreviewPublisher(&previewPublisher_);
    }

    int PluginVideoRecordHost::Run()
    {
        if (!ipcServer_.Start())
        {
            return 1;
        }

        previewPublisher_.Start(ipcServer_.GetSessionId());
        ipcServer_.SetAvailableBackendMask(GraphicsBackendMask_None);
        ipcServer_.SetSelectedBackend(GraphicsBackend::Unknown);
        ipcServer_.SetActiveRecordingBackend(GraphicsBackend::Unknown);
        PublishState(RecorderState::Standby, L"", L"", 0);
        LogMessage(L"宿主已启动，等待图形 Hook。");

        vulkanLayerModule_ = IsCurrentModuleVulkanLayer(moduleHandle_);
        const bool installOk = vulkanLayerModule_
            ? InstallLayerModeHooks()
            : InstallDxHooks();
        if (!installOk)
        {
            LogMessage(L"图形 Hook 安装失败。");
            ShutdownHooks();
            previewPublisher_.Stop();
            ipcServer_.Stop();
            return 1;
        }

        while (!stopRequested_.load())
        {
            PollBackendDiagnostics();
            PollPendingRuntimeCommand();
            Sleep(50);
        }

        recorder_.Stop();
        SetActiveRecordingBackend(GraphicsBackend::Unknown);
        ShutdownHooks();
        ipcServer_.SetAvailableBackendMask(GraphicsBackendMask_None);
        ipcServer_.SetSelectedBackend(GraphicsBackend::Unknown);
        ipcServer_.SetActiveRecordingBackend(GraphicsBackend::Unknown);
        previewPublisher_.Stop();
        ipcServer_.Stop();
        return 0;
    }

    void PluginVideoRecordHost::RequestStop()
    {
        stopRequested_.store(true);
    }

    void PluginVideoRecordHost::OnAutoFrame(const UrhAutoHookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleFrame(runtime);
        }
    }

    void PluginVideoRecordHost::OnVulkanFrame(const UrhVulkanHookRuntime* runtime, void* userData)
    {
        auto* self = static_cast<PluginVideoRecordHost*>(userData);
        if (self)
        {
            self->HandleFrame(runtime);
        }
    }

}
