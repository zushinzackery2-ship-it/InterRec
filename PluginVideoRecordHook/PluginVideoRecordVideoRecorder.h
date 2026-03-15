#pragma once

#include "PluginVideoRecordDx11Capture.h"
#include "PluginVideoRecordDx12Capture.h"
#include "PluginVideoRecordMfWriter.h"
#include "PluginVideoRecordProcessAudioCapture.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordPreviewPublisher;

    class PluginVideoRecordVideoRecorder
    {
    public:
        PluginVideoRecordVideoRecorder();
        ~PluginVideoRecordVideoRecorder();

        void SetPreviewPublisher(PluginVideoRecordPreviewPublisher* previewPublisher);
        bool Start(const RainGuiDx11HookRuntime* runtime, std::wstring& outputPath, std::wstring& error);
        bool Start(const RainGuiDx12HookRuntime* runtime, std::wstring& outputPath, std::wstring& error);
        void Stop();
        bool OnFrame(const RainGuiDx11HookRuntime* runtime, std::wstring& error);
        bool OnFrame(const RainGuiDx12HookRuntime* runtime, std::wstring& error);
        bool IsRecording() const;
        std::wstring GetCurrentOutputPath() const;
        std::wstring GetPendingOutputPath() const;

    private:
        bool BuildOutputPath(std::wstring& outputPath, std::wstring& error) const;
        bool BuildPendingOutputPath(std::wstring& outputPath) const;
        LONGLONG GetSampleTimeHns() const;
        bool StartWriterAndAudio(UINT width, UINT height, const std::wstring& outputPath, std::wstring& error);
        void StopUnlocked();

        mutable std::mutex mutex_;
        PluginVideoRecordDx11Capture dx11Capture_;
        PluginVideoRecordDx12Capture dx12Capture_;
        PluginVideoRecordProcessAudioCapture audioCapture_;
        PluginVideoRecordMfWriter writer_;
        LARGE_INTEGER performanceFrequency_;
        LARGE_INTEGER startCounter_;
        LONGLONG startQpcHns_;
        bool qpcReady_;
        bool recording_;
        GraphicsBackend activeBackend_;
        PluginVideoRecordPreviewPublisher* previewPublisher_;
        std::wstring currentOutputPath_;
    };
}
