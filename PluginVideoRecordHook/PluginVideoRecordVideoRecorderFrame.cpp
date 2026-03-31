#include "pch.h"

#include "PluginVideoRecordPreviewPublisher.h"
#include "PluginVideoRecordVideoRecorder.h"

namespace PluginVideoRecord
{
    bool PluginVideoRecordVideoRecorder::Start(
        const UrhDx11HookRuntime* runtime,
        std::wstring& outputPath,
        std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();

        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->deviceContext)
        {
            error = L"DX11 运行时还没准备好，暂时不能开始录制。";
            return false;
        }

        if (!dx11Capture_.Initialize(runtime, error))
        {
            return false;
        }

        if (!BuildOutputPath(outputPath, error))
        {
            dx11Capture_.Shutdown();
            return false;
        }

        if (!StartWriterAndAudio(dx11Capture_.GetCaptureWidth(), dx11Capture_.GetCaptureHeight(), outputPath, error))
        {
            dx11Capture_.Shutdown();
            return false;
        }

        currentOutputPath_ = outputPath;
        recording_ = true;
        activeBackend_ = GraphicsBackend::Dx11;

        if (previewPublisher_)
        {
            previewPublisher_->Clear();
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::Start(
        const UrhDx12HookRuntime* runtime,
        std::wstring& outputPath,
        std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();

        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->commandQueue)
        {
            error = L"DX12 运行时还没准备好，暂时不能开始录制。";
            return false;
        }

        if (!dx12Capture_.Initialize(runtime, error))
        {
            return false;
        }

        if (!BuildOutputPath(outputPath, error))
        {
            dx12Capture_.Shutdown();
            return false;
        }

        if (!StartWriterAndAudio(dx12Capture_.GetCaptureWidth(), dx12Capture_.GetCaptureHeight(), outputPath, error))
        {
            dx12Capture_.Shutdown();
            return false;
        }

        currentOutputPath_ = outputPath;
        recording_ = true;
        activeBackend_ = GraphicsBackend::Dx12;

        if (previewPublisher_)
        {
            previewPublisher_->Clear();
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::Start(
        const UrhVulkanHookRuntime* runtime,
        std::wstring& outputPath,
        std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();

        if (!runtime || !runtime->swapchain || !runtime->device || !runtime->queue)
        {
            error = L"Vulkan 运行时还没准备好，暂时不能开始录制。";
            return false;
        }

        if (!vulkanCapture_.Initialize(runtime, error))
        {
            return false;
        }

        if (!BuildOutputPath(outputPath, error))
        {
            vulkanCapture_.Shutdown();
            return false;
        }

        if (!StartWriterAndAudio(vulkanCapture_.GetCaptureWidth(), vulkanCapture_.GetCaptureHeight(), outputPath, error))
        {
            vulkanCapture_.Shutdown();
            return false;
        }

        currentOutputPath_ = outputPath;
        recording_ = true;
        activeBackend_ = GraphicsBackend::Vulkan;

        if (previewPublisher_)
        {
            previewPublisher_->Clear();
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::OnFrame(const UrhDx11HookRuntime* runtime, std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recording_)
        {
            return true;
        }

        if (activeBackend_ != GraphicsBackend::Dx11)
        {
            error = L"当前录制后端与 DX11 Hook 不匹配。";
            return false;
        }

        if (audioCapture_.TryGetLastError(error))
        {
            return false;
        }

        CapturedFrame frame = {};
        if (!dx11Capture_.CaptureFrame(runtime, GetSampleTimeHns(), frame, error))
        {
            return false;
        }

        if (previewPublisher_)
        {
            previewPublisher_->PublishFrame(frame);
        }

        if (!writer_.EnqueueFrame(std::move(frame)) && writer_.TryGetLastError(error))
        {
            return false;
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::OnFrame(const UrhDx12HookRuntime* runtime, std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recording_)
        {
            return true;
        }

        if (activeBackend_ != GraphicsBackend::Dx12)
        {
            error = L"当前录制后端与 DX12 Hook 不匹配。";
            return false;
        }

        if (audioCapture_.TryGetLastError(error))
        {
            return false;
        }

        CapturedFrame frame = {};
        if (!dx12Capture_.CaptureFrame(runtime, GetSampleTimeHns(), frame, error))
        {
            return false;
        }

        if (previewPublisher_)
        {
            previewPublisher_->PublishFrame(frame);
        }

        if (!writer_.EnqueueFrame(std::move(frame)) && writer_.TryGetLastError(error))
        {
            return false;
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::OnFrame(const UrhVulkanHookRuntime* runtime, std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recording_)
        {
            return true;
        }

        if (activeBackend_ != GraphicsBackend::Vulkan)
        {
            error = L"当前录制后端与 Vulkan Hook 不匹配。";
            return false;
        }

        if (audioCapture_.TryGetLastError(error))
        {
            return false;
        }

        CapturedFrame frame = {};
        if (!vulkanCapture_.CaptureFrame(runtime, GetSampleTimeHns(), frame, error))
        {
            return false;
        }

        if (frame.pixels.empty())
        {
            return true;
        }

        if (previewPublisher_)
        {
            previewPublisher_->PublishFrame(frame);
        }

        if (!writer_.EnqueueFrame(std::move(frame)) && writer_.TryGetLastError(error))
        {
            return false;
        }

        return true;
    }
}
