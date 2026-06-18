#include "pch.h"

#include "PluginVideoRecordDx12Capture.h"

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

}

namespace PluginVideoRecord
{
    bool PluginVideoRecordDx12Capture::CreateReadbackResources(
        const UrhDx12HookRuntime* runtime,
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
