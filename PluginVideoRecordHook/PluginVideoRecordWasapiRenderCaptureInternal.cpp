#include "pch.h"

#include "PluginVideoRecordWasapiRenderCaptureInternal.h"
#include "PluginVideoRecordWasapiRenderHook.h"

namespace PluginVideoRecord::WasapiRenderCaptureInternal
{
    bool InstallHooks(std::wstring& error)
    {
        CaptureRuntime& runtime = Runtime();
        {
            std::lock_guard<std::mutex> lock(runtime.mutex);
            if (runtime.hooksInstalled)
            {
                return true;
            }
        }

        if (!PluginVideoRecord::InstallWasapiRenderHooks(error))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(runtime.mutex);
        runtime.hooksInstalled = true;
        return true;
    }

    void ShutdownHooks()
    {
        {
            CaptureRuntime& runtime = Runtime();
            std::unique_lock<std::mutex> lock(runtime.mutex);
            runtime.recording = false;
            runtime.writer = nullptr;
            ++runtime.activeSessionId;
            ClearQueueLocked(runtime);
            runtime.condition.wait(
                lock,
                [&runtime]()
                {
                    return runtime.processingCount == 0;
                });
        }

        StopWorker();
        PluginVideoRecord::ShutdownWasapiRenderHooks();

        CaptureRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        runtime.hooksInstalled = false;
    }

    void SubmitRenderBuffer(WasapiRenderBuffer&& renderBuffer)
    {
        CaptureRuntime& runtime = Runtime();
        const size_t byteCount = GetQueuedBytes(renderBuffer);

        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (!runtime.recording || runtime.failed || !runtime.writer)
        {
            return;
        }

        if (byteCount == 0 || byteCount > RawQueueBudgetBytes)
        {
            SetFailureLocked(runtime, L"WASAPI render 音频包大小异常。");
            return;
        }

        if (runtime.queuedBytes + byteCount > RawQueueBudgetBytes)
        {
            SetFailureLocked(runtime, L"WASAPI render 音频采集队列溢出。");
            return;
        }

        QueuedRenderBuffer item = {};
        item.renderBuffer = std::move(renderBuffer);
        item.sessionId = runtime.activeSessionId;
        runtime.queuedBytes += byteCount;
        runtime.queue.push_back(std::move(item));
        runtime.condition.notify_all();
    }

    void ReportFailure(const std::wstring& error)
    {
        CaptureRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (runtime.recording)
        {
            SetFailureLocked(runtime, error);
        }
    }

    bool Start(PluginVideoRecordMfWriter* writer, std::wstring& error)
    {
        if (!writer)
        {
            error = L"WASAPI render 音频采集缺少写入器。";
            return false;
        }

        if (!InstallHooks(error) || !EnsureWorkerStarted(error))
        {
            return false;
        }

        CaptureRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        ++runtime.activeSessionId;
        runtime.writer = writer;
        runtime.recording = true;
        runtime.failed = false;
        runtime.lastError.clear();
        runtime.nextSampleTimeHns = 0;
        ClearQueueLocked(runtime);
        return true;
    }

    void Stop()
    {
        CaptureRuntime& runtime = Runtime();
        std::unique_lock<std::mutex> lock(runtime.mutex);
        runtime.recording = false;
        runtime.writer = nullptr;
        ++runtime.activeSessionId;
        ClearQueueLocked(runtime);
        runtime.condition.wait(
            lock,
            [&runtime]()
            {
                return runtime.processingCount == 0;
            });
    }

    bool TryGetLastError(std::wstring& error)
    {
        CaptureRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (!runtime.failed)
        {
            return false;
        }

        error = runtime.lastError;
        return true;
    }
}
