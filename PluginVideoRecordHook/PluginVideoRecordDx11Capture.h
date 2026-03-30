#pragma once

#include <urh/dx11_types.h>

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordDx11Capture
    {
    public:
        PluginVideoRecordDx11Capture();

        bool Initialize(const UrhDx11HookRuntime* runtime, std::wstring& error);
        void Shutdown();
        bool CaptureFrame(
            const UrhDx11HookRuntime* runtime,
            LONGLONG sampleTimeHns,
            CapturedFrame& frame,
            std::wstring& error);
        bool MatchesRuntime(const UrhDx11HookRuntime* runtime) const;
        UINT GetCaptureWidth() const;
        UINT GetCaptureHeight() const;

    private:
        bool RebindRuntime(const UrhDx11HookRuntime* runtime, std::wstring& error);
        void ConvertPixels(const D3D11_MAPPED_SUBRESOURCE& mappedResource, CapturedFrame& frame) const;
        bool IsSupportedFormat(DXGI_FORMAT format) const;

        Microsoft::WRL::ComPtr<ID3D11Device> device_;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture_;
        DXGI_FORMAT format_;
        UINT sourceWidth_;
        UINT sourceHeight_;
        UINT captureWidth_;
        UINT captureHeight_;
    };
}
