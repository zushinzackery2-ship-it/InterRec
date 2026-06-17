#pragma once

#include "PluginVideoRecordWasapiRenderCapture.h"

namespace PluginVideoRecord::WasapiRenderCaptureInternal
{
    constexpr size_t RawQueueBudgetBytes = 16ull * 1024ull * 1024ull;

    struct QueuedRenderBuffer
    {
        WasapiRenderBuffer renderBuffer;
        ULONGLONG sessionId;
    };

    struct CaptureRuntime
    {
        std::mutex mutex;
        std::condition_variable condition;
        std::thread workerThread;
        std::deque<QueuedRenderBuffer> queue;
        PluginVideoRecordMfWriter* writer = nullptr;
        bool workerRunning = false;
        bool workerStopRequested = false;
        bool recording = false;
        bool failed = false;
        bool hooksInstalled = false;
        size_t queuedBytes = 0;
        size_t processingCount = 0;
        ULONGLONG activeSessionId = 0;
        LONGLONG nextSampleTimeHns = 0;
        std::wstring lastError;
    };

    CaptureRuntime& Runtime();
    size_t GetQueuedBytes(const WasapiRenderBuffer& renderBuffer);
    void ClearQueueLocked(CaptureRuntime& runtime);
    void SetFailureLocked(CaptureRuntime& runtime, const std::wstring& error);
    bool EnsureWorkerStarted(std::wstring& error);
    void StopWorker();

    bool InstallHooks(std::wstring& error);
    void ShutdownHooks();
    void SubmitRenderBuffer(WasapiRenderBuffer&& renderBuffer);
    void ReportFailure(const std::wstring& error);
    bool Start(PluginVideoRecordMfWriter* writer, std::wstring& error);
    void Stop();
    bool TryGetLastError(std::wstring& error);
}
