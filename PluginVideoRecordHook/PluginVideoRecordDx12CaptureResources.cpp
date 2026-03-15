#include "pch.h"

#include "PluginVideoRecordDx12Capture.h"

#include <cmath>

namespace
{
    std::wstring BuildDx12ResourceError(const wchar_t* text, HRESULT hr)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (0x%08X)", text, static_cast<unsigned int>(hr));
        return buffer;
    }

    const wchar_t* GetDx12FormatName(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return L"R8G8B8A8_UNORM";

        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return L"R8G8B8A8_UNORM_SRGB";

        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return L"B8G8R8A8_UNORM";

        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return L"B8G8R8A8_UNORM_SRGB";

        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return L"B8G8R8X8_UNORM";

        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return L"B8G8R8X8_UNORM_SRGB";

        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return L"R10G10B10A2_UNORM";

        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return L"R16G16B16A16_FLOAT";

        default:
            return L"UNKNOWN";
        }
    }

    std::wstring BuildDx12UnsupportedFormatError(DXGI_FORMAT format)
    {
        wchar_t buffer[256] = {};
        swprintf_s(
            buffer,
            L"当前 DX12 后缓冲格式不在录屏支持范围内：%ls (%u)。",
            GetDx12FormatName(format),
            static_cast<unsigned int>(format));
        return buffer;
    }

    std::uint8_t ConvertUnorm10ToByte(std::uint32_t value)
    {
        return static_cast<std::uint8_t>((value * 255u + 511u) / 1023u);
    }

    float ClampToUnit(float value)
    {
        if (value < 0.0f)
        {
            return 0.0f;
        }

        if (value > 1.0f)
        {
            return 1.0f;
        }

        return value;
    }

    float ConvertHalfToFloat(std::uint16_t value)
    {
        const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000u) << 16;
        std::uint32_t exponent = (value >> 10) & 0x1Fu;
        std::uint32_t mantissa = value & 0x03FFu;
        std::uint32_t resultBits = 0;

        if (exponent == 0)
        {
            if (mantissa == 0)
            {
                resultBits = sign;
            }
            else
            {
                exponent = 1;
                while ((mantissa & 0x0400u) == 0)
                {
                    mantissa <<= 1;
                    --exponent;
                }

                mantissa &= 0x03FFu;
                exponent += 112;
                resultBits = sign | (exponent << 23) | (mantissa << 13);
            }
        }
        else if (exponent == 31)
        {
            resultBits = sign | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            exponent += 112;
            resultBits = sign | (exponent << 23) | (mantissa << 13);
        }

        float result = 0.0f;
        CopyMemory(&result, &resultBits, sizeof(result));
        return result;
    }

    std::uint8_t ConvertFloatToSdrByte(float value)
    {
        value = value < 0.0f ? 0.0f : value;

        // DX12 的 FP16 后缓冲常见于 HDR/线性空间，这里先压到 SDR，保证录屏至少能稳定输出。
        const float mapped = value / (1.0f + value);
        const float srgb = mapped <= 0.0031308f
            ? mapped * 12.92f
            : 1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(ClampToUnit(srgb) * 255.0f + 0.5f);
    }
}

namespace PluginVideoRecord
{
    bool PluginVideoRecordDx12Capture::CreateReadbackResources(
        const RainGuiDx12HookRuntime* runtime,
        std::wstring& error)
    {
        UINT bufferIndex = runtime->bufferCount > runtime->backBufferIndex ? runtime->backBufferIndex : 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        HRESULT hr = runtime->swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法读取 DX12 交换链缓冲。", hr);
            return false;
        }

        D3D12_RESOURCE_DESC description = backBuffer->GetDesc();
        if (!IsSupportedFormat(description.Format))
        {
            error = BuildDx12UnsupportedFormatError(description.Format);
            return false;
        }

        if (description.SampleDesc.Count != 1)
        {
            error = L"当前 DX12 交换链使用了多重采样，暂不支持直接录制。";
            return false;
        }

        captureWidth_ = static_cast<UINT>(description.Width) & ~1u;
        captureHeight_ = description.Height & ~1u;
        if (captureWidth_ == 0 || captureHeight_ == 0)
        {
            error = L"DX12 录屏尺寸无效。";
            return false;
        }

        device_ = runtime->device;
        commandQueue_ = runtime->commandQueue;
        format_ = description.Format;
        sourceWidth_ = static_cast<UINT>(description.Width);
        sourceHeight_ = description.Height;

        hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法创建 DX12 录屏命令分配器。", hr);
            return false;
        }

        hr = device_->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            commandAllocator_.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList_));
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法创建 DX12 录屏命令列表。", hr);
            return false;
        }

        hr = commandList_->Close();
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法初始化 DX12 录屏命令列表。", hr);
            return false;
        }

        hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法创建 DX12 录屏同步栅栏。", hr);
            return false;
        }

        fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent_)
        {
            error = L"无法创建 DX12 录屏同步事件。";
            return false;
        }

        device_->GetCopyableFootprints(&description, 0, 1, 0, &footprint_, nullptr, nullptr, nullptr);

        D3D12_HEAP_PROPERTIES heapProperties = {};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDescription = {};
        bufferDescription.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDescription.Width = footprint_.Footprint.RowPitch * captureHeight_;
        bufferDescription.Height = 1;
        bufferDescription.DepthOrArraySize = 1;
        bufferDescription.MipLevels = 1;
        bufferDescription.SampleDesc.Count = 1;
        bufferDescription.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device_->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &bufferDescription,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readbackBuffer_));
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法创建 DX12 读回缓冲。", hr);
            return false;
        }

        return true;
    }

    bool PluginVideoRecordDx12Capture::WaitForCopy(std::wstring& error)
    {
        const UINT64 waitValue = ++fenceValue_;
        HRESULT hr = commandQueue_->Signal(fence_.Get(), waitValue);
        if (FAILED(hr))
        {
            error = BuildDx12ResourceError(L"无法提交 DX12 录屏同步信号。", hr);
            return false;
        }

        if (fence_->GetCompletedValue() < waitValue)
        {
            hr = fence_->SetEventOnCompletion(waitValue, fenceEvent_);
            if (FAILED(hr))
            {
                error = BuildDx12ResourceError(L"无法等待 DX12 录屏同步栅栏。", hr);
                return false;
            }

            if (WaitForSingleObject(fenceEvent_, 5000) != WAIT_OBJECT_0)
            {
                error = L"等待 DX12 录屏读回超时。";
                return false;
            }
        }

        return true;
    }

    void PluginVideoRecordDx12Capture::ConvertPixels(const std::uint8_t* sourcePixels, CapturedFrame& frame) const
    {
        for (UINT y = 0; y < captureHeight_; ++y)
        {
            auto* sourceRow = sourcePixels + static_cast<size_t>(y) * footprint_.Footprint.RowPitch;
            auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * captureWidth_ * 4u;

            for (UINT x = 0; x < captureWidth_; ++x)
            {
                if (format_ == DXGI_FORMAT_B8G8R8A8_UNORM ||
                    format_ == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                    format_ == DXGI_FORMAT_B8G8R8X8_UNORM ||
                    format_ == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
                {
                    const size_t pixelOffset = static_cast<size_t>(x) * 4u;
                    destinationRow[pixelOffset + 0] = sourceRow[pixelOffset + 0];
                    destinationRow[pixelOffset + 1] = sourceRow[pixelOffset + 1];
                    destinationRow[pixelOffset + 2] = sourceRow[pixelOffset + 2];
                    destinationRow[pixelOffset + 3] = 0xFF;
                }
                else if (format_ == DXGI_FORMAT_R10G10B10A2_UNORM)
                {
                    const size_t pixelOffset = static_cast<size_t>(x) * 4u;
                    std::uint32_t packedPixel = 0;
                    CopyMemory(&packedPixel, sourceRow + pixelOffset, sizeof(packedPixel));
                    destinationRow[pixelOffset + 0] = ConvertUnorm10ToByte((packedPixel >> 20) & 0x3FFu);
                    destinationRow[pixelOffset + 1] = ConvertUnorm10ToByte((packedPixel >> 10) & 0x3FFu);
                    destinationRow[pixelOffset + 2] = ConvertUnorm10ToByte(packedPixel & 0x3FFu);
                    destinationRow[pixelOffset + 3] = 0xFF;
                }
                else if (format_ == DXGI_FORMAT_R16G16B16A16_FLOAT)
                {
                    const size_t sourcePixelOffset = static_cast<size_t>(x) * 8u;
                    const size_t destinationPixelOffset = static_cast<size_t>(x) * 4u;
                    const auto* halfPixel =
                        reinterpret_cast<const std::uint16_t*>(sourceRow + sourcePixelOffset);
                    destinationRow[destinationPixelOffset + 0] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[2]));
                    destinationRow[destinationPixelOffset + 1] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[1]));
                    destinationRow[destinationPixelOffset + 2] = ConvertFloatToSdrByte(ConvertHalfToFloat(halfPixel[0]));
                    destinationRow[destinationPixelOffset + 3] = 0xFF;
                }
                else
                {
                    const size_t pixelOffset = static_cast<size_t>(x) * 4u;
                    destinationRow[pixelOffset + 0] = sourceRow[pixelOffset + 2];
                    destinationRow[pixelOffset + 1] = sourceRow[pixelOffset + 1];
                    destinationRow[pixelOffset + 2] = sourceRow[pixelOffset + 0];
                    destinationRow[pixelOffset + 3] = 0xFF;
                }
            }
        }
    }

    bool PluginVideoRecordDx12Capture::IsSupportedFormat(DXGI_FORMAT format) const
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return true;

        default:
            return false;
        }
    }
}
