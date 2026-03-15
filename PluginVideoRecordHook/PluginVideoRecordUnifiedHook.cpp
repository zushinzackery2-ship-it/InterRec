#include "pch.h"

#include "../../RainGui/RainGui/DX11Hook/raingui_dx11hook_internal.h"
#include "../../RainGui/RainGui/DX12Hook/raingui_dx12hook_internal.h"

#include "PluginVideoRecordUnifiedHook.h"

namespace
{
    PluginVideoRecord::PluginVideoRecordUnifiedHook* g_unifiedHookInstance = nullptr;
    constexpr DWORD UninstallWaitTimeoutMs = 5000;
}

namespace PluginVideoRecord
{
    PluginVideoRecordUnifiedHook::PluginVideoRecordUnifiedHook()
        : presentHookCount_(0)
        , executeCommandListsVtable_(nullptr)
        , originalExecuteCommandLists_(nullptr)
        , presentInFlight_(0)
        , unloading_(false)
        , installed_(false)
    {
        ZeroMemory(&callbacks_, sizeof(callbacks_));
        ZeroMemory(presentHooks_, sizeof(presentHooks_));
    }

    PluginVideoRecordUnifiedHook::~PluginVideoRecordUnifiedHook()
    {
        Uninstall();
    }

    bool PluginVideoRecordUnifiedHook::Install(const Callbacks& callbacks, std::wstring& error)
    {
        if (installed_)
        {
            return true;
        }

        callbacks_ = callbacks;
        unloading_.store(false);
        presentInFlight_.store(0);
        presentHookCount_ = 0;
        executeCommandListsVtable_ = nullptr;
        originalExecuteCommandLists_ = nullptr;

        RainGuiDx11HookInternal::Dx11HookProbeData dx11Probe = {};
        RainGuiDx12HookInternal::Dx12HookProbeData dx12Probe = {};
        const bool hasDx11Probe = RainGuiDx11HookInternal::ProbeVtables(dx11Probe);
        const bool hasDx12Probe = RainGuiDx12HookInternal::ProbeVtables(dx12Probe);
        if (!hasDx11Probe && !hasDx12Probe)
        {
            error = L"统一图形 Hook 探测失败。";
            return false;
        }

        g_unifiedHookInstance = this;

        if (hasDx11Probe && !InstallPresentHook(dx11Probe.swapChainVtable, presentHooks_[presentHookCount_].originalPresent))
        {
            error = L"安装 DX11 Present Hook 失败。";
            Uninstall();
            return false;
        }

        if (hasDx12Probe && !InstallPresentHook(dx12Probe.swapChainVtable, presentHooks_[presentHookCount_].originalPresent))
        {
            error = L"安装 DX12 Present Hook 失败。";
            Uninstall();
            return false;
        }

        if (hasDx12Probe && dx12Probe.commandQueueVtable)
        {
            if (!RainGuiDx12HookInternal::PatchVtable(
                    dx12Probe.commandQueueVtable,
                    10,
                    reinterpret_cast<void*>(&HookExecuteCommandLists),
                    reinterpret_cast<void**>(&originalExecuteCommandLists_)))
            {
                error = L"安装 DX12 ExecuteCommandLists Hook 失败。";
                Uninstall();
                return false;
            }

            executeCommandListsVtable_ = dx12Probe.commandQueueVtable;
        }

        installed_ = presentHookCount_ > 0;
        if (!installed_)
        {
            error = L"没有可用的图形 Present Hook。";
            Uninstall();
            return false;
        }

        return true;
    }

    void PluginVideoRecordUnifiedHook::Uninstall()
    {
        if (!installed_ && !executeCommandListsVtable_ && presentHookCount_ == 0)
        {
            g_unifiedHookInstance = nullptr;
            return;
        }

        unloading_.store(true);

        DWORD waitedMs = 0;
        while (presentInFlight_.load() > 0 && waitedMs < UninstallWaitTimeoutMs)
        {
            Sleep(10);
            waitedMs += 10;
        }

        RestoreExecuteHook();
        RestorePresentHooks();

        {
            std::lock_guard<std::mutex> lock(pendingQueueMutex_);
            pendingQueue_.Reset();
        }

        g_unifiedHookInstance = nullptr;
        installed_ = false;
        unloading_.store(false);
        ZeroMemory(&callbacks_, sizeof(callbacks_));
    }

    bool PluginVideoRecordUnifiedHook::IsInstalled() const
    {
        return installed_;
    }

    bool PluginVideoRecordUnifiedHook::InstallPresentHook(void** vtable, PresentFn& originalPresent)
    {
        if (!vtable)
        {
            return false;
        }

        for (size_t index = 0; index < presentHookCount_; ++index)
        {
            if (presentHooks_[index].vtable == vtable)
            {
                originalPresent = presentHooks_[index].originalPresent;
                return true;
            }
        }

        PresentFn currentOriginal = nullptr;
        if (!RainGuiDx11HookInternal::PatchVtable(
                vtable,
                8,
                reinterpret_cast<void*>(&HookPresent),
                reinterpret_cast<void**>(&currentOriginal)))
        {
            return false;
        }

        presentHooks_[presentHookCount_].vtable = vtable;
        presentHooks_[presentHookCount_].originalPresent = currentOriginal;
        originalPresent = currentOriginal;
        ++presentHookCount_;
        return true;
    }

    void PluginVideoRecordUnifiedHook::RestorePresentHooks()
    {
        for (size_t index = 0; index < presentHookCount_; ++index)
        {
            if (presentHooks_[index].vtable && presentHooks_[index].originalPresent)
            {
                RainGuiDx11HookInternal::RestoreVtable(
                    presentHooks_[index].vtable,
                    8,
                    reinterpret_cast<void*>(presentHooks_[index].originalPresent));
            }
        }

        ZeroMemory(presentHooks_, sizeof(presentHooks_));
        presentHookCount_ = 0;
    }

    void PluginVideoRecordUnifiedHook::RestoreExecuteHook()
    {
        if (executeCommandListsVtable_ && originalExecuteCommandLists_)
        {
            RainGuiDx12HookInternal::RestoreVtable(
                executeCommandListsVtable_,
                10,
                reinterpret_cast<void*>(originalExecuteCommandLists_));
        }

        executeCommandListsVtable_ = nullptr;
        originalExecuteCommandLists_ = nullptr;
    }

    PluginVideoRecordUnifiedHook::PresentFn PluginVideoRecordUnifiedHook::ResolveOriginalPresent(
        IDXGISwapChain* swapChain) const
    {
        if (!swapChain)
        {
            return nullptr;
        }

        auto** vtable = *reinterpret_cast<void***>(swapChain);
        for (size_t index = 0; index < presentHookCount_; ++index)
        {
            if (presentHooks_[index].vtable == vtable)
            {
                return presentHooks_[index].originalPresent;
            }
        }

        return nullptr;
    }

    PluginVideoRecordUnifiedHook::ExecuteCommandListsFn
        PluginVideoRecordUnifiedHook::ResolveOriginalExecuteCommandLists() const
    {
        return originalExecuteCommandLists_;
    }

    Microsoft::WRL::ComPtr<ID3D12CommandQueue> PluginVideoRecordUnifiedHook::AcquirePendingQueue() const
    {
        std::lock_guard<std::mutex> lock(pendingQueueMutex_);
        return pendingQueue_;
    }

    HRESULT STDMETHODCALLTYPE PluginVideoRecordUnifiedHook::HookPresent(
        IDXGISwapChain* swapChain,
        UINT syncInterval,
        UINT flags)
    {
        auto* self = g_unifiedHookInstance;
        if (!self)
        {
            return E_FAIL;
        }

        PresentFn originalPresent = self->ResolveOriginalPresent(swapChain);
        if (!originalPresent)
        {
            return E_FAIL;
        }

        if (self->unloading_.load() || (flags & DXGI_PRESENT_TEST) != 0)
        {
            return originalPresent(swapChain, syncInterval, flags);
        }

        self->presentInFlight_.fetch_add(1);
        self->HandlePresent(swapChain);
        HRESULT hr = originalPresent(swapChain, syncInterval, flags);
        self->presentInFlight_.fetch_sub(1);
        return hr;
    }

    void STDMETHODCALLTYPE PluginVideoRecordUnifiedHook::HookExecuteCommandLists(
        ID3D12CommandQueue* queue,
        UINT numCommandLists,
        ID3D12CommandList* const* commandLists)
    {
        auto* self = g_unifiedHookInstance;
        if (!self)
        {
            return;
        }

        if (!self->unloading_.load() && queue)
        {
            D3D12_COMMAND_QUEUE_DESC description = queue->GetDesc();
            if (description.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                std::lock_guard<std::mutex> lock(self->pendingQueueMutex_);
                self->pendingQueue_ = queue;
            }
        }

        ExecuteCommandListsFn originalExecute = self->ResolveOriginalExecuteCommandLists();
        if (originalExecute)
        {
            originalExecute(queue, numCommandLists, commandLists);
        }
    }
}
