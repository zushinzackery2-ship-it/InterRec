#pragma once

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordPreviewPublisher
    {
    public:
        PluginVideoRecordPreviewPublisher();
        ~PluginVideoRecordPreviewPublisher();

        bool Start();
        void Stop();
        void Clear();
        void PublishFrame(const CapturedFrame& frame);

    private:
        void CloseHandles();
        void ClearUnlocked();
        void WriteScaledFrame(const CapturedFrame& frame) const;

        HANDLE mappingHandle_;
        PreviewFrame* previewView_;
        ULONGLONG lastPublishTick_;
        std::mutex mutex_;
    };
}
