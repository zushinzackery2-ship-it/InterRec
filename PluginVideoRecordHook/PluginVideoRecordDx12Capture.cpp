#include "pch.h"

#include "PluginVideoRecordDx12Capture.h"

namespace
{
    std::wstring BuildDx12Error(const wchar_t* text, HRESULT hr)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (0x%08X)", text, static_cast<unsigned int>(hr));
        return buffer;
    }
}

namespace PluginVideoRecord
{
    PluginVideoRecordDx12Capture::PluginVideoRecordDx12Capture()
        : fenceEvent_(nullptr)
        , fenceValue_(0)
        , format_(DXGI_FORMAT_UNKNOWN)
        , sourceWidth_(0)
        , sourceHeight_(0)
        , captureWidth_(0)
        , captureHeight_(0)
    {
        ZeroMemory(&footprint_, sizeof(footprint_));
    }

    PluginVideoRecordDx12Capture::~PluginVideoRecordDx12Capture()
    {
        Shutdown();
    }

    bool PluginVideoRecordDx12Capture::Initialize(const RainGuiDx12HookRuntime* runtime, std::wstring& error)
    {
        Shutdown();

        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->commandQueue)
        {
            error = L"DX12 录屏初始化失败，运行时对象为空。";
            return false;
        }

        return CreateReadbackResources(runtime, error);
    }

    void PluginVideoRecordDx12Capture::Shutdown()
    {
        readbackBuffer_.Reset();
        fence_.Reset();
        commandList_.Reset();
        commandAllocator_.Reset();
        commandQueue_.Reset();
        device_.Reset();

        if (fenceEvent_)
        {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }

        fenceValue_ = 0;
        ZeroMemory(&footprint_, sizeof(footprint_));
        format_ = DXGI_FORMAT_UNKNOWN;
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        captureWidth_ = 0;
        captureHeight_ = 0;
    }

    bool PluginVideoRecordDx12Capture::CaptureFrame(
        const RainGuiDx12HookRuntime* runtime,
        LONGLONG sampleTimeHns,
        CapturedFrame& frame,
        std::wstring& error)
    {
        if (!MatchesRuntime(runtime))
        {
            error = L"交换链尺寸、格式或命令队列已经变化。";
            return false;
        }

        UINT bufferIndex = runtime->bufferCount > runtime->backBufferIndex ? runtime->backBufferIndex : 0;
        Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
        HRESULT hr = runtime->swapChain->GetBuffer(bufferIndex, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
        {
            error = BuildDx12Error(L"无法获取 DX12 交换链后缓冲。", hr);
            return false;
        }

        hr = commandAllocator_->Reset();
        if (FAILED(hr))
        {
            error = BuildDx12Error(L"无法重置 DX12 录屏命令分配器。", hr);
            return false;
        }

        hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
        if (FAILED(hr))
        {
            error = BuildDx12Error(L"无法重置 DX12 录屏命令列表。", hr);
            return false;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        commandList_->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION destination = {};
        destination.pResource = readbackBuffer_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint_;

        D3D12_TEXTURE_COPY_LOCATION source = {};
        source.pResource = backBuffer.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = 0;
        commandList_->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList_->ResourceBarrier(1, &barrier);

        hr = commandList_->Close();
        if (FAILED(hr))
        {
            error = BuildDx12Error(L"无法关闭 DX12 录屏命令列表。", hr);
            return false;
        }

        ID3D12CommandList* commandLists[] = { commandList_.Get() };
        commandQueue_->ExecuteCommandLists(1, commandLists);
        if (!WaitForCopy(error))
        {
            return false;
        }

        std::uint8_t* mappedPixels = nullptr;
        D3D12_RANGE readRange = { 0, footprint_.Footprint.RowPitch * captureHeight_ };
        hr = readbackBuffer_->Map(0, &readRange, reinterpret_cast<void**>(&mappedPixels));
        if (FAILED(hr))
        {
            error = BuildDx12Error(L"无法映射 DX12 读回缓冲。", hr);
            return false;
        }

        frame.width = captureWidth_;
        frame.height = captureHeight_;
        frame.sampleTimeHns = sampleTimeHns;
        frame.pixels.resize(static_cast<size_t>(captureWidth_) * captureHeight_ * 4u);
        ConvertPixels(mappedPixels, frame);

        D3D12_RANGE writeRange = { 0, 0 };
        readbackBuffer_->Unmap(0, &writeRange);
        return true;
    }

    bool PluginVideoRecordDx12Capture::MatchesRuntime(const RainGuiDx12HookRuntime* runtime) const
    {
        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->commandQueue)
        {
            return false;
        }

        UINT width = runtime->width > 0 ? static_cast<UINT>(runtime->width) : 0;
        UINT height = runtime->height > 0 ? static_cast<UINT>(runtime->height) : 0;
        return runtime->device == device_.Get() &&
            runtime->commandQueue == commandQueue_.Get() &&
            width == sourceWidth_ &&
            height == sourceHeight_ &&
            runtime->backBufferFormat == format_;
    }

    UINT PluginVideoRecordDx12Capture::GetCaptureWidth() const
    {
        return captureWidth_;
    }

    UINT PluginVideoRecordDx12Capture::GetCaptureHeight() const
    {
        return captureHeight_;
    }

}
