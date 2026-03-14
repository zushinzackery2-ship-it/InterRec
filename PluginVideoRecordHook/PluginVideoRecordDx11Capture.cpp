#include "pch.h"

#include "PluginVideoRecordDx11Capture.h"

namespace
{
    std::wstring BuildDxError(const wchar_t* text, HRESULT hr)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (0x%08X)", text, static_cast<unsigned int>(hr));
        return buffer;
    }
}

namespace PluginVideoRecord
{
    PluginVideoRecordDx11Capture::PluginVideoRecordDx11Capture()
        : format_(DXGI_FORMAT_UNKNOWN)
        , sourceWidth_(0)
        , sourceHeight_(0)
        , captureWidth_(0)
        , captureHeight_(0)
    {
    }

    bool PluginVideoRecordDx11Capture::Initialize(const RainGuiDx11HookRuntime* runtime, std::wstring& error)
    {
        Shutdown();

        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->deviceContext)
        {
            error = L"DX11 录屏初始化失败，运行时对象为空。";
            return false;
        }

        UINT bufferIndex = runtime->bufferCount > runtime->backBufferIndex ? runtime->backBufferIndex : 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = runtime->swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            error = BuildDxError(L"无法获取交换链后缓冲。", hr);
            return false;
        }

        D3D11_TEXTURE2D_DESC description = {};
        backBuffer->GetDesc(&description);
        if (!IsSupportedFormat(description.Format))
        {
            error = L"当前后缓冲格式不在录屏支持范围内。";
            return false;
        }

        if (description.SampleDesc.Count != 1)
        {
            error = L"当前交换链使用了多重采样，暂不支持直接录制。";
            return false;
        }

        D3D11_TEXTURE2D_DESC stagingDescription = description;
        stagingDescription.Usage = D3D11_USAGE_STAGING;
        stagingDescription.BindFlags = 0;
        stagingDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDescription.MiscFlags = 0;

        hr = runtime->device->CreateTexture2D(&stagingDescription, nullptr, &stagingTexture_);
        if (FAILED(hr))
        {
            error = BuildDxError(L"无法创建录屏暂存纹理。", hr);
            return false;
        }

        device_ = runtime->device;
        deviceContext_ = runtime->deviceContext;
        format_ = description.Format;
        sourceWidth_ = description.Width;
        sourceHeight_ = description.Height;
        captureWidth_ = description.Width & ~1u;
        captureHeight_ = description.Height & ~1u;

        if (captureWidth_ == 0 || captureHeight_ == 0)
        {
            error = L"录屏尺寸无效。";
            Shutdown();
            return false;
        }

        return true;
    }

    void PluginVideoRecordDx11Capture::Shutdown()
    {
        stagingTexture_.Reset();
        deviceContext_.Reset();
        device_.Reset();
        format_ = DXGI_FORMAT_UNKNOWN;
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        captureWidth_ = 0;
        captureHeight_ = 0;
    }

    bool PluginVideoRecordDx11Capture::CaptureFrame(
        const RainGuiDx11HookRuntime* runtime,
        LONGLONG sampleTimeHns,
        CapturedFrame& frame,
        std::wstring& error)
    {
        if (!MatchesRuntime(runtime))
        {
            error = L"交换链尺寸或格式已经变化。";
            return false;
        }

        UINT bufferIndex = runtime->bufferCount > runtime->backBufferIndex ? runtime->backBufferIndex : 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HRESULT hr = runtime->swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            error = BuildDxError(L"读取交换链缓冲失败。", hr);
            return false;
        }

        deviceContext_->CopyResource(stagingTexture_.Get(), backBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mappedResource = {};
        hr = deviceContext_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr))
        {
            error = BuildDxError(L"映射录屏缓冲失败。", hr);
            return false;
        }

        frame.width = captureWidth_;
        frame.height = captureHeight_;
        frame.sampleTimeHns = sampleTimeHns;
        frame.pixels.resize(static_cast<size_t>(captureWidth_) * captureHeight_ * 4u);
        ConvertPixels(mappedResource, frame);
        deviceContext_->Unmap(stagingTexture_.Get(), 0);
        return true;
    }

    bool PluginVideoRecordDx11Capture::MatchesRuntime(const RainGuiDx11HookRuntime* runtime) const
    {
        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->deviceContext)
        {
            return false;
        }

        UINT width = runtime->width > 0 ? static_cast<UINT>(runtime->width) : 0;
        UINT height = runtime->height > 0 ? static_cast<UINT>(runtime->height) : 0;
        return runtime->device == device_.Get() &&
            runtime->deviceContext == deviceContext_.Get() &&
            width == sourceWidth_ &&
            height == sourceHeight_ &&
            runtime->backBufferFormat == format_;
    }

    UINT PluginVideoRecordDx11Capture::GetCaptureWidth() const
    {
        return captureWidth_;
    }

    UINT PluginVideoRecordDx11Capture::GetCaptureHeight() const
    {
        return captureHeight_;
    }

    void PluginVideoRecordDx11Capture::ConvertPixels(
        const D3D11_MAPPED_SUBRESOURCE& mappedResource,
        CapturedFrame& frame) const
    {
        for (UINT y = 0; y < captureHeight_; ++y)
        {
            auto* sourceRow = static_cast<const std::uint8_t*>(mappedResource.pData) + y * mappedResource.RowPitch;
            auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * captureWidth_ * 4u;

            for (UINT x = 0; x < captureWidth_; ++x)
            {
                const size_t sourceOffset = static_cast<size_t>(x) * 4u;
                const size_t destinationOffset = sourceOffset;

                if (format_ == DXGI_FORMAT_B8G8R8A8_UNORM || format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                {
                    destinationRow[destinationOffset + 0] = sourceRow[sourceOffset + 0];
                    destinationRow[destinationOffset + 1] = sourceRow[sourceOffset + 1];
                    destinationRow[destinationOffset + 2] = sourceRow[sourceOffset + 2];
                }
                else
                {
                    destinationRow[destinationOffset + 0] = sourceRow[sourceOffset + 2];
                    destinationRow[destinationOffset + 1] = sourceRow[sourceOffset + 1];
                    destinationRow[destinationOffset + 2] = sourceRow[sourceOffset + 0];
                }

                destinationRow[destinationOffset + 3] = 0xFF;
            }
        }
    }

    bool PluginVideoRecordDx11Capture::IsSupportedFormat(DXGI_FORMAT format) const
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return true;

        default:
            return false;
        }
    }
}
