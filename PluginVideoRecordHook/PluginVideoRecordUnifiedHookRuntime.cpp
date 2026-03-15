#include "pch.h"

#include "PluginVideoRecordUnifiedHook.h"

namespace
{
    void FillSwapChainSnapshot(
        IDXGISwapChain* swapChain,
        UINT& bufferCount,
        UINT& backBufferIndex,
        DXGI_FORMAT& backBufferFormat,
        float& width,
        float& height)
    {
        bufferCount = 0;
        backBufferIndex = 0;
        backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        width = 0.0f;
        height = 0.0f;

        DXGI_SWAP_CHAIN_DESC description = {};
        if (swapChain && SUCCEEDED(swapChain->GetDesc(&description)))
        {
            bufferCount = description.BufferCount;
            width = static_cast<float>(description.BufferDesc.Width);
            height = static_cast<float>(description.BufferDesc.Height);
            backBufferFormat = description.BufferDesc.Format == DXGI_FORMAT_UNKNOWN
                ? DXGI_FORMAT_R8G8B8A8_UNORM
                : description.BufferDesc.Format;
        }

        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        if (swapChain && SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))))
        {
            backBufferIndex = swapChain3->GetCurrentBackBufferIndex();
        }
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordUnifiedHook::HandlePresent(IDXGISwapChain* swapChain)
    {
        if (!swapChain)
        {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D11Device> dx11Device;
        if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&dx11Device))) && dx11Device)
        {
            Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
            dx11Device->GetImmediateContext(&deviceContext);
            if (!deviceContext || !callbacks_.onDx11Frame)
            {
                return;
            }

            RainGuiDx11HookRuntime runtime = {};
            runtime.hwnd = nullptr;
            runtime.swapChain = swapChain;
            runtime.device = dx11Device.Get();
            runtime.deviceContext = deviceContext.Get();
            FillSwapChainSnapshot(
                swapChain,
                runtime.bufferCount,
                runtime.backBufferIndex,
                runtime.backBufferFormat,
                runtime.width,
                runtime.height);

            DXGI_SWAP_CHAIN_DESC description = {};
            if (SUCCEEDED(swapChain->GetDesc(&description)))
            {
                runtime.hwnd = description.OutputWindow;
            }

            callbacks_.onDx11Frame(&runtime, callbacks_.userData);
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12Device> dx12Device;
        if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&dx12Device))) || !dx12Device || !callbacks_.onDx12Frame)
        {
            return;
        }

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue = AcquirePendingQueue();
        RainGuiDx12HookRuntime runtime = {};
        runtime.hwnd = nullptr;
        runtime.swapChain = swapChain;
        runtime.device = dx12Device.Get();
        runtime.commandQueue = commandQueue.Get();
        FillSwapChainSnapshot(
            swapChain,
            runtime.bufferCount,
            runtime.backBufferIndex,
            runtime.backBufferFormat,
            runtime.width,
            runtime.height);

        DXGI_SWAP_CHAIN_DESC description = {};
        if (SUCCEEDED(swapChain->GetDesc(&description)))
        {
            runtime.hwnd = description.OutputWindow;
        }

        callbacks_.onDx12Frame(&runtime, callbacks_.userData);
    }
}
