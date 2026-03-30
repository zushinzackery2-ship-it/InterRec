#include "pch.h"

#include <vkh/vkh.h>

#include "PluginVideoRecordHost.h"

namespace
{
    PluginVideoRecord::GraphicsBackend ConvertAutoHookBackend(UrhAutoHookBackend backend)
    {
        switch (backend)
        {
        case UrhAutoHookBackend_Dx11:
            return PluginVideoRecord::GraphicsBackend::Dx11;

        case UrhAutoHookBackend_Dx12:
            return PluginVideoRecord::GraphicsBackend::Dx12;

        case UrhAutoHookBackend_Vulkan:
            return PluginVideoRecord::GraphicsBackend::Vulkan;

        default:
            return PluginVideoRecord::GraphicsBackend::Unknown;
        }
    }

    const wchar_t* GetBackendName(PluginVideoRecord::GraphicsBackend graphicsBackend)
    {
        switch (graphicsBackend)
        {
        case PluginVideoRecord::GraphicsBackend::Dx11:
            return L"DX11";

        case PluginVideoRecord::GraphicsBackend::Dx12:
            return L"DX12";

        case PluginVideoRecord::GraphicsBackend::Vulkan:
            return L"Vulkan";

        default:
            return L"Unknown";
        }
    }

    std::wstring BuildAutoHookSnapshotMessage(const UrhAutoHookDiagnostics& diagnostics)
    {
        wchar_t buffer[640] = {};
        swprintf_s(
            buffer,
            L"AutoHook 诊断快照：locked=%ls；DX11 seen=%d stable=%u size=%.0fx%.0f；"
            L"DX12 seen=%d stable=%u size=%.0fx%.0f；Vulkan seen=%d stable=%u size=%.0fx%.0f。",
            GetBackendName(ConvertAutoHookBackend(diagnostics.lockedBackend)),
            diagnostics.dx11Seen ? 1 : 0,
            diagnostics.dx11StableFrames,
            diagnostics.dx11Width,
            diagnostics.dx11Height,
            diagnostics.dx12Seen ? 1 : 0,
            diagnostics.dx12StableFrames,
            diagnostics.dx12Width,
            diagnostics.dx12Height,
            diagnostics.vulkanSeen ? 1 : 0,
            diagnostics.vulkanStableFrames,
            diagnostics.vulkanWidth,
            diagnostics.vulkanHeight);
        return buffer;
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordHost::PollBackendDiagnostics()
    {
        LONG nextAvailableBackendMask = GraphicsBackendMask_None;

        if (autoHookInstalled_)
        {
            UrhAutoHookDiagnostics diagnostics = {};
            URH::GetDiagnostics(&diagnostics);

            const GraphicsBackend lockedDxBackend = ConvertAutoHookBackend(diagnostics.lockedBackend);
            if (lockedDxBackend == GraphicsBackend::Dx11 || lockedDxBackend == GraphicsBackend::Dx12)
            {
                nextAvailableBackendMask |= GetGraphicsBackendMask(lockedDxBackend);
            }

            if (lockedDxBackend != lastAutoHookBackend_)
            {
                lastAutoHookBackend_ = lockedDxBackend;
                if (lockedDxBackend != GraphicsBackend::Unknown)
                {
                    wchar_t buffer[96] = {};
                    swprintf_s(buffer, L"已识别图形后端：%ls。", GetBackendName(lockedDxBackend));
                    LogMessage(buffer);
                }

                LogMessage(BuildAutoHookSnapshotMessage(diagnostics));
            }
        }

        if (vulkanHookInstalled_)
        {
            const bool hasRecognizedBackend = VHK::HasRecognizedBackend();
            const bool isReady = VHK::IsReady();
            const bool hasRuntime = VHK::GetRuntime() != nullptr;

            if (hasRecognizedBackend != lastVulkanRecognizedState_ ||
                isReady != lastVulkanReadyState_ ||
                hasRuntime != lastVulkanRuntimeState_)
            {
                wchar_t buffer[256] = {};
                swprintf_s(
                    buffer,
                    L"VulkanHook 状态：recognized=%d ready=%d runtime=%d availableMask=%ld selected=%ls active=%ls。",
                    hasRecognizedBackend ? 1 : 0,
                    isReady ? 1 : 0,
                    hasRuntime ? 1 : 0,
                    nextAvailableBackendMask,
                    GetBackendName(selectedBackend_),
                    GetBackendName(activeRecordingBackend_));
                LogMessage(buffer);

                lastVulkanRecognizedState_ = hasRecognizedBackend;
                lastVulkanReadyState_ = isReady;
                lastVulkanRuntimeState_ = hasRuntime;
            }

            if (hasRecognizedBackend)
            {
                nextAvailableBackendMask |= GraphicsBackendMask_Vulkan;
            }
        }

        if (availableBackendMask_ != nextAvailableBackendMask)
        {
            availableBackendMask_ = nextAvailableBackendMask;
            ipcServer_.SetAvailableBackendMask(availableBackendMask_);
        }

        EnsureSelectedBackend();
    }

    void PluginVideoRecordHost::UpdateBackendAvailability(GraphicsBackend graphicsBackend, bool available)
    {
        if (!available)
        {
            return;
        }

        const LONG backendMask = GetGraphicsBackendMask(graphicsBackend);
        if ((availableBackendMask_ & backendMask) == 0)
        {
            availableBackendMask_ |= backendMask;
            ipcServer_.SetAvailableBackendMask(availableBackendMask_);
        }
    }

    void PluginVideoRecordHost::EnsureSelectedBackend()
    {
        GraphicsBackend nextSelectedBackend = ipcServer_.GetSelectedBackend();
        if (nextSelectedBackend == GraphicsBackend::Unknown)
        {
            nextSelectedBackend = ChooseDefaultGraphicsBackend(availableBackendMask_);
        }

        if (selectedBackend_ == nextSelectedBackend)
        {
            return;
        }

        selectedBackend_ = nextSelectedBackend;
        ipcServer_.SetSelectedBackend(selectedBackend_);

        if (selectedBackend_ != GraphicsBackend::Unknown)
        {
            wchar_t buffer[96] = {};
            swprintf_s(buffer, L"录制后端已选择：%ls。", GetBackendName(selectedBackend_));
            LogMessage(buffer);
        }
    }
}
