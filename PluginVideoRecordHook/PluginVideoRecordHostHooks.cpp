#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace PluginVideoRecord
{
    bool PluginVideoRecordHost::InstallAutoHook()
    {
        UrhAutoHookDesc desc = {};
        URH::FillDefaultDesc(&desc);
        desc.onRender = OnAutoFrame;
        desc.userData = this;
        desc.autoCreateContext = false;
        desc.hookWndProc = false;
        desc.blockInputWhenVisible = false;
        desc.enableDefaultDebugWindow = false;
        desc.backendMask =
            UrhAutoHookBackendMask_Dx11 |
            UrhAutoHookBackendMask_Dx12;
        desc.warmupFrames = 0;

        autoHookInstalled_ = URH::Init(&desc);
        if (autoHookInstalled_)
        {
            LogMessage(L"DX AutoHook 安装成功，等待 DX11/DX12 命中。");
        }

        return autoHookInstalled_;
    }

    bool PluginVideoRecordHost::InstallVulkanHook()
    {
        URH::VulkanDesc desc = {};
        URH::FillVulkanDefaultDesc(&desc);
        desc.onRender = OnVulkanFrame;
        desc.userData = this;
        desc.autoCreateContext = false;
        desc.hookWndProc = false;
        desc.blockInputWhenVisible = false;
        desc.enableDefaultDebugWindow = false;
        desc.warmupFrames = 0;

        vulkanHookInstalled_ = URH::InitVulkan(&desc);
        if (vulkanHookInstalled_)
        {
            LogMessage(L"Vulkan Layer Hook 安装成功。");
        }

        return vulkanHookInstalled_;
    }

    bool PluginVideoRecordHost::InstallDxHooks()
    {
        return InstallAutoHook();
    }

    bool PluginVideoRecordHost::InstallLayerModeHooks()
    {
        const bool vulkanInstallOk = InstallVulkanHook();
        const bool dxInstallOk = InstallAutoHook();
        if (vulkanInstallOk || dxInstallOk)
        {
            LogMessage(L"Vulkan-Layer模式已启用，并保留 DX AutoHook 回退。");
            return true;
        }

        return false;
    }

    void PluginVideoRecordHost::ShutdownHooks()
    {
        if (autoHookInstalled_)
        {
            URH::Shutdown();
            autoHookInstalled_ = false;
        }

        if (vulkanHookInstalled_)
        {
            URH::ShutdownVulkan();
            vulkanHookInstalled_ = false;
        }
    }

    bool PluginVideoRecordHost::IsVulkanRuntimeReady() const
    {
        if (!vulkanHookInstalled_ || !URH::IsVulkanReady())
        {
            return false;
        }

        return IsVulkanRuntimeUsable(URH::GetVulkanRuntime());
    }

    bool PluginVideoRecordHost::IsVulkanRuntimeUsable(const UrhVulkanHookRuntime* runtime)
    {
        return runtime &&
            runtime->swapchain &&
            runtime->device &&
            runtime->queue &&
            runtime->width > 0.0f &&
            runtime->height > 0.0f;
    }
}
