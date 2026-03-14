#pragma once

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordProcessAudioCapture
    {
    public:
        PluginVideoRecordProcessAudioCapture();
        ~PluginVideoRecordProcessAudioCapture();

        bool Start(
            DWORD processId,
            LONGLONG recordingStartQpcHns,
            PluginVideoRecordMfWriter* writer,
            std::wstring& error);
        void Stop();
        bool TryGetLastError(std::wstring& error) const;

    private:
        void CaptureThread();
        void SetFailure(const std::wstring& error);

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::thread captureThread_;
        HANDLE stopEvent_;
        DWORD processId_;
        LONGLONG recordingStartQpcHns_;
        PluginVideoRecordMfWriter* writer_;
        bool running_;
        bool stopRequested_;
        bool startupCompleted_;
        bool failed_;
        std::wstring lastError_;
    };
}
