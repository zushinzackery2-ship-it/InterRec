#include "pch.h"

#include "PluginVideoRecordPreviewPublisher.h"

namespace
{
    constexpr ULONGLONG PreviewIntervalMs = 33;
}

namespace PluginVideoRecord
{
    PluginVideoRecordPreviewPublisher::PluginVideoRecordPreviewPublisher()
        : mappingHandle_(nullptr)
        , previewView_(nullptr)
        , lastPublishTick_(0)
    {
    }

    PluginVideoRecordPreviewPublisher::~PluginVideoRecordPreviewPublisher()
    {
        Stop();
    }

    bool PluginVideoRecordPreviewPublisher::Start()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        CloseHandles();

        mappingHandle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(PreviewFrame),
            PreviewMappingName);
        if (!mappingHandle_)
        {
            return false;
        }

        previewView_ = static_cast<PreviewFrame*>(
            MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(PreviewFrame)));
        if (!previewView_)
        {
            CloseHandles();
            return false;
        }

        ClearUnlocked();
        return true;
    }

    void PluginVideoRecordPreviewPublisher::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (previewView_)
        {
            ClearUnlocked();
        }

        CloseHandles();
        lastPublishTick_ = 0;
    }

    void PluginVideoRecordPreviewPublisher::Clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearUnlocked();
        lastPublishTick_ = 0;
    }

    void PluginVideoRecordPreviewPublisher::PublishFrame(const CapturedFrame& frame)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!previewView_ || frame.width == 0 || frame.height == 0)
        {
            return;
        }

        const size_t expectedSize = static_cast<size_t>(frame.width) * frame.height * 4u;
        if (frame.pixels.size() < expectedSize)
        {
            return;
        }

        ULONGLONG now = GetTickCount64();
        if (lastPublishTick_ != 0 && now - lastPublishTick_ < PreviewIntervalMs)
        {
            return;
        }

        InterlockedIncrement(&previewView_->frameSequence);
        previewView_->protocolVersion = ProtocolVersion;
        previewView_->width = PreviewWidth;
        previewView_->height = PreviewHeight;
        previewView_->stride = PreviewStride;
        previewView_->sampleTimeHns = frame.sampleTimeHns;
        WriteScaledFrame(frame);
        InterlockedExchange(&previewView_->isValid, TRUE);
        InterlockedIncrement(&previewView_->frameSequence);
        lastPublishTick_ = now;
    }

    void PluginVideoRecordPreviewPublisher::CloseHandles()
    {
        if (previewView_)
        {
            UnmapViewOfFile(previewView_);
            previewView_ = nullptr;
        }

        if (mappingHandle_)
        {
            CloseHandle(mappingHandle_);
            mappingHandle_ = nullptr;
        }
    }

    void PluginVideoRecordPreviewPublisher::ClearUnlocked()
    {
        if (!previewView_)
        {
            return;
        }

        InterlockedIncrement(&previewView_->frameSequence);
        previewView_->protocolVersion = ProtocolVersion;
        InterlockedExchange(&previewView_->isValid, FALSE);
        previewView_->width = PreviewWidth;
        previewView_->height = PreviewHeight;
        previewView_->stride = PreviewStride;
        previewView_->sampleTimeHns = 0;
        ZeroMemory(previewView_->pixels, sizeof(previewView_->pixels));
        InterlockedIncrement(&previewView_->frameSequence);
    }

    void PluginVideoRecordPreviewPublisher::WriteScaledFrame(const CapturedFrame& frame) const
    {
        ZeroMemory(previewView_->pixels, sizeof(previewView_->pixels));

        UINT scaledWidth = PreviewWidth;
        UINT scaledHeight = static_cast<UINT>(
            static_cast<ULONGLONG>(frame.height) * PreviewWidth / std::max(frame.width, 1u));

        if (scaledHeight > PreviewHeight)
        {
            scaledHeight = PreviewHeight;
            scaledWidth = static_cast<UINT>(
                static_cast<ULONGLONG>(frame.width) * PreviewHeight / std::max(frame.height, 1u));
        }

        scaledWidth = std::clamp(scaledWidth, 1u, PreviewWidth);
        scaledHeight = std::clamp(scaledHeight, 1u, PreviewHeight);

        const UINT offsetX = (PreviewWidth - scaledWidth) / 2;
        const UINT offsetY = (PreviewHeight - scaledHeight) / 2;

        for (UINT y = 0; y < scaledHeight; ++y)
        {
            UINT sourceY = static_cast<UINT>(static_cast<ULONGLONG>(y) * frame.height / scaledHeight);
            auto* destinationRow = previewView_->pixels +
                static_cast<size_t>(y + offsetY) * PreviewStride +
                static_cast<size_t>(offsetX) * 4u;

            for (UINT x = 0; x < scaledWidth; ++x)
            {
                UINT sourceX = static_cast<UINT>(static_cast<ULONGLONG>(x) * frame.width / scaledWidth);
                const size_t sourceOffset =
                    (static_cast<size_t>(sourceY) * frame.width + sourceX) * 4u;
                const size_t destinationOffset = static_cast<size_t>(x) * 4u;

                destinationRow[destinationOffset + 0] = frame.pixels[sourceOffset + 0];
                destinationRow[destinationOffset + 1] = frame.pixels[sourceOffset + 1];
                destinationRow[destinationOffset + 2] = frame.pixels[sourceOffset + 2];
                destinationRow[destinationOffset + 3] = 0xFF;
            }
        }
    }
}
