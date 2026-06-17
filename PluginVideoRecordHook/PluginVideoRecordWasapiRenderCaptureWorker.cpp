#include "pch.h"

#include "PluginVideoRecordInternalLogger.h"
#include "PluginVideoRecordWasapiRenderCaptureInternal.h"

namespace
{
    void LogCaptureFailure(const std::wstring& error)
    {
        char buffer[512] = {};
        const int length = WideCharToMultiByte(
            CP_UTF8,
            0,
            error.c_str(),
            -1,
            buffer,
            static_cast<int>(sizeof(buffer)),
            nullptr,
            nullptr);
        if (length > 0)
        {
            PvrcInternalLogger::Log("[PVRC][WasapiRenderCapture] failure: %s", buffer);
        }
        else
        {
            PvrcInternalLogger::Log("[PVRC][WasapiRenderCapture] failure");
        }
    }

    bool PopNextBuffer(
        PluginVideoRecord::WasapiRenderCaptureInternal::CaptureRuntime& runtime,
        PluginVideoRecord::WasapiRenderCaptureInternal::QueuedRenderBuffer& item)
    {
        std::unique_lock<std::mutex> lock(runtime.mutex);
        runtime.condition.wait(
            lock,
            [&runtime]()
            {
                return runtime.workerStopRequested || !runtime.queue.empty();
            });

        if (runtime.workerStopRequested && runtime.queue.empty())
        {
            return false;
        }

        item = std::move(runtime.queue.front());
        runtime.queuedBytes -= std::min(
            runtime.queuedBytes,
            PluginVideoRecord::WasapiRenderCaptureInternal::GetQueuedBytes(item.renderBuffer));
        runtime.queue.pop_front();
        ++runtime.processingCount;
        return true;
    }

    bool PreparePacket(
        PluginVideoRecord::WasapiRenderCaptureInternal::CaptureRuntime& runtime,
        const PluginVideoRecord::WasapiRenderCaptureInternal::QueuedRenderBuffer& item,
        LONGLONG packetDurationHns,
        LONGLONG& sampleTimeHns,
        PluginVideoRecord::PluginVideoRecordMfWriter*& writer)
    {
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (!runtime.recording ||
            runtime.failed ||
            item.sessionId != runtime.activeSessionId ||
            !runtime.writer)
        {
            return false;
        }

        sampleTimeHns = runtime.nextSampleTimeHns;
        runtime.nextSampleTimeHns += packetDurationHns;
        writer = runtime.writer;
        return true;
    }

    void FinishProcessing(PluginVideoRecord::WasapiRenderCaptureInternal::CaptureRuntime& runtime)
    {
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (runtime.processingCount > 0)
        {
            --runtime.processingCount;
        }

        runtime.condition.notify_all();
    }

    void WorkerThread()
    {
        namespace Internal = PluginVideoRecord::WasapiRenderCaptureInternal;

        PvrcInternalLogger::Log("[PVRC][WasapiRenderCapture] worker begin");

        Internal::CaptureRuntime& runtime = Internal::Runtime();
        for (;;)
        {
            Internal::QueuedRenderBuffer item = {};
            if (!PopNextBuffer(runtime, item))
            {
                break;
            }

            PluginVideoRecord::CapturedAudioPacket packet = {};
            if (PluginVideoRecord::ConvertWasapiRenderBufferToCapturedPacket(item.renderBuffer, 0, packet))
            {
                LONGLONG sampleTimeHns = 0;
                PluginVideoRecord::PluginVideoRecordMfWriter* writer = nullptr;
                const bool shouldProcess =
                    packet.durationHns > 0 &&
                    PreparePacket(runtime, item, packet.durationHns, sampleTimeHns, writer);

                if (shouldProcess)
                {
                    packet.sampleTimeHns = sampleTimeHns;
                    if (!writer->EnqueueAudioPacket(std::move(packet)))
                    {
                        std::wstring writerError;
                        if (writer->TryGetLastError(writerError))
                        {
                            Internal::ReportFailure(writerError);
                        }
                        else
                        {
                            Internal::ReportFailure(L"WASAPI render 音频包写入队列失败。");
                        }
                    }
                }
            }
            else
            {
                Internal::ReportFailure(L"WASAPI render 音频格式转换失败。");
            }

            FinishProcessing(runtime);
        }

        PvrcInternalLogger::Log("[PVRC][WasapiRenderCapture] worker end");
    }
}

namespace PluginVideoRecord::WasapiRenderCaptureInternal
{
    CaptureRuntime& Runtime()
    {
        static CaptureRuntime runtime;
        return runtime;
    }

    size_t GetQueuedBytes(const WasapiRenderBuffer& renderBuffer)
    {
        return PluginVideoRecord::GetWasapiRenderBufferByteCount(
            renderBuffer.format,
            renderBuffer.frameCount);
    }

    void ClearQueueLocked(CaptureRuntime& runtime)
    {
        runtime.queue.clear();
        runtime.queuedBytes = 0;
    }

    void SetFailureLocked(CaptureRuntime& runtime, const std::wstring& error)
    {
        if (runtime.failed)
        {
            return;
        }

        runtime.failed = true;
        runtime.lastError = error;
        LogCaptureFailure(error);
        ClearQueueLocked(runtime);
        runtime.condition.notify_all();
    }

    bool EnsureWorkerStarted(std::wstring& error)
    {
        CaptureRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        if (runtime.workerRunning)
        {
            return true;
        }

        try
        {
            runtime.workerStopRequested = false;
            runtime.workerThread = std::thread(WorkerThread);
            runtime.workerRunning = true;
            return true;
        }
        catch (...)
        {
            error = L"无法启动 WASAPI render 音频采集线程。";
            return false;
        }
    }

    void StopWorker()
    {
        CaptureRuntime& runtime = Runtime();
        {
            std::lock_guard<std::mutex> lock(runtime.mutex);
            runtime.workerStopRequested = true;
            runtime.condition.notify_all();
        }

        if (runtime.workerThread.joinable())
        {
            runtime.workerThread.join();
        }

        std::lock_guard<std::mutex> lock(runtime.mutex);
        runtime.workerRunning = false;
        runtime.workerStopRequested = false;
        runtime.processingCount = 0;
        ClearQueueLocked(runtime);
    }
}
