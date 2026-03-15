#pragma once

#include "../../RainGui/RainGui/raingui_dx11hook_types.h"
#include "../../RainGui/RainGui/raingui_dx12hook_types.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordUnifiedHook
    {
    public:
        struct Callbacks
        {
            void (*onDx11Frame)(const RainGuiDx11HookRuntime* runtime, void* userData);
            void (*onDx12Frame)(const RainGuiDx12HookRuntime* runtime, void* userData);
            void* userData;
        };

        PluginVideoRecordUnifiedHook();
        ~PluginVideoRecordUnifiedHook();

        bool Install(const Callbacks& callbacks, std::wstring& error);
        void Uninstall();
        bool IsInstalled() const;

    private:
        typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
        typedef void(STDMETHODCALLTYPE* ExecuteCommandListsFn)(
            ID3D12CommandQueue* queue,
            UINT numCommandLists,
            ID3D12CommandList* const* commandLists);

        struct PresentHookEntry
        {
            void** vtable;
            PresentFn originalPresent;
        };

        static HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);
        static void STDMETHODCALLTYPE HookExecuteCommandLists(
            ID3D12CommandQueue* queue,
            UINT numCommandLists,
            ID3D12CommandList* const* commandLists);

        bool InstallPresentHook(void** vtable, PresentFn& originalPresent);
        void RestorePresentHooks();
        void RestoreExecuteHook();
        PresentFn ResolveOriginalPresent(IDXGISwapChain* swapChain) const;
        void HandlePresent(IDXGISwapChain* swapChain);
        ExecuteCommandListsFn ResolveOriginalExecuteCommandLists() const;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> AcquirePendingQueue() const;

        Callbacks callbacks_;
        PresentHookEntry presentHooks_[2];
        size_t presentHookCount_;
        void** executeCommandListsVtable_;
        ExecuteCommandListsFn originalExecuteCommandLists_;
        std::atomic<LONG> presentInFlight_;
        std::atomic<bool> unloading_;
        bool installed_;
        mutable std::mutex pendingQueueMutex_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> pendingQueue_;
    };
}
