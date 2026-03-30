#pragma once

#include <urh/dx12_types.h>

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordDx12Capture
    {
    public:
        PluginVideoRecordDx12Capture();
        ~PluginVideoRecordDx12Capture();

        bool Initialize(const UrhDx12HookRuntime* runtime, std::wstring& error);
        void Shutdown();
        bool CaptureFrame(
            const UrhDx12HookRuntime* runtime,
            LONGLONG sampleTimeHns,
            CapturedFrame& frame,
            std::wstring& error);
        bool MatchesRuntime(const UrhDx12HookRuntime* runtime) const;
        UINT GetCaptureWidth() const;
        UINT GetCaptureHeight() const;

    private:
        bool RebindRuntime(const UrhDx12HookRuntime* runtime, std::wstring& error);
        bool CreateReadbackResources(const UrhDx12HookRuntime* runtime, std::wstring& error);
        bool WaitForCopy(std::wstring& error);
        void ConvertPixels(const std::uint8_t* sourcePixels, CapturedFrame& frame) const;
        bool IsSupportedFormat(DXGI_FORMAT format) const;

        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer_;
        HANDLE fenceEvent_;
        UINT64 fenceValue_;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_;
        DXGI_FORMAT format_;
        UINT sourceWidth_;
        UINT sourceHeight_;
        UINT captureWidth_;
        UINT captureHeight_;
    };
}
