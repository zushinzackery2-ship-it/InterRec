#pragma once

#include "PluginVideoRecordDx11Capture.h"
#include "PluginVideoRecordMfWriter.h"
#include "PluginVideoRecordProcessAudioCapture.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordVideoRecorder
    {
    public:
        PluginVideoRecordVideoRecorder();
        ~PluginVideoRecordVideoRecorder();

        bool Start(const RainGuiDx11HookRuntime* runtime, std::wstring& outputPath, std::wstring& error);
        void Stop();
        bool OnFrame(const RainGuiDx11HookRuntime* runtime, std::wstring& error);
        bool IsRecording() const;
        std::wstring GetCurrentOutputPath() const;

    private:
        bool BuildOutputPath(std::wstring& outputPath, std::wstring& error) const;
        LONGLONG GetSampleTimeHns() const;
        void StopUnlocked();

        mutable std::mutex mutex_;
        PluginVideoRecordDx11Capture capture_;
        PluginVideoRecordProcessAudioCapture audioCapture_;
        PluginVideoRecordMfWriter writer_;
        LARGE_INTEGER performanceFrequency_;
        LARGE_INTEGER startCounter_;
        LONGLONG startQpcHns_;
        bool qpcReady_;
        bool recording_;
        std::wstring currentOutputPath_;
    };
}
