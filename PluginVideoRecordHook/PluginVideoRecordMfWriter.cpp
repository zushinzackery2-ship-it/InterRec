#include "pch.h"

#include "PluginVideoRecordMfInterop.h"
#include "PluginVideoRecordMfWriter.h"

namespace
{
    bool HasPendingSamples(
        const std::deque<PluginVideoRecord::CapturedFrame>& videoFrames,
        const std::deque<PluginVideoRecord::CapturedAudioPacket>& audioPackets)
    {
        return !videoFrames.empty() || !audioPackets.empty();
    }
}

namespace PluginVideoRecord
{
    PluginVideoRecordMfWriter::PluginVideoRecordMfWriter()
        : width_(0)
        , height_(0)
        , started_(false)
        , stopping_(false)
        , failed_(false)
        , startupCompleted_(false)
    {
    }

    PluginVideoRecordMfWriter::~PluginVideoRecordMfWriter()
    {
        Stop();
    }

    bool PluginVideoRecordMfWriter::Start(const std::wstring& outputPath, UINT width, UINT height, std::wstring& error)
    {
        Stop();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            outputPath_ = outputPath;
            width_ = width;
            height_ = height;
            frames_.clear();
            audioPackets_.clear();
            started_ = true;
            stopping_ = false;
            failed_ = false;
            startupCompleted_ = false;
            lastError_.clear();
        }

        workerThread_ = std::thread(&PluginVideoRecordMfWriter::WorkerThread, this);

        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return startupCompleted_; });
        if (failed_)
        {
            error = lastError_;
            lock.unlock();
            Stop();
            return false;
        }

        return true;
    }

    void PluginVideoRecordMfWriter::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_ && !workerThread_.joinable())
            {
                return;
            }

            stopping_ = true;
            condition_.notify_all();
        }

        if (workerThread_.joinable())
        {
            workerThread_.join();
        }
    }

    bool PluginVideoRecordMfWriter::EnqueueFrame(CapturedFrame&& frame)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || stopping_ || failed_)
        {
            return false;
        }

        if (frames_.size() >= MaxQueuedFrames)
        {
            frames_.pop_front();
        }

        frames_.emplace_back(std::move(frame));
        condition_.notify_all();
        return true;
    }

    bool PluginVideoRecordMfWriter::EnqueueAudioPacket(CapturedAudioPacket&& packet)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_ || stopping_ || failed_)
        {
            return false;
        }

        if (audioPackets_.size() >= MaxQueuedAudioPackets)
        {
            failed_ = true;
            lastError_ = L"音频写入队列积压过多，已停止录制。";
            condition_.notify_all();
            return false;
        }

        audioPackets_.emplace_back(std::move(packet));
        condition_.notify_all();
        return true;
    }

    bool PluginVideoRecordMfWriter::TryGetLastError(std::wstring& error) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_)
        {
            return false;
        }

        error = lastError_;
        return true;
    }

    void PluginVideoRecordMfWriter::WorkerThread()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            SetFailure(BuildMfError(L"无法初始化 COM。", hr));
            return;
        }

        bool mediaFoundationReady = false;
        hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (SUCCEEDED(hr))
        {
            mediaFoundationReady = true;
        }
        else
        {
            SetFailure(BuildMfError(L"无法启动 Media Foundation。", hr));
        }

        Microsoft::WRL::ComPtr<IMFSinkWriter> sinkWriter;
        SinkWriterStreams streamIndices = {};
        if (!failed_)
        {
            hr = CreateSinkWriter(outputPath_, width_, height_, sinkWriter, streamIndices);
            if (FAILED(hr))
            {
                SetFailure(BuildMfError(L"创建 MP4 输出失败。", hr));
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            startupCompleted_ = true;
            condition_.notify_all();
        }

        LONGLONG lastVideoSampleTime = -1;
        while (!failed_)
        {
            bool writeVideo = false;
            CapturedFrame videoFrame = {};
            CapturedAudioPacket audioPacket = {};

            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]()
                    {
                        return failed_ || stopping_ || HasPendingSamples(frames_, audioPackets_);
                    });

                if (failed_ || (stopping_ && !HasPendingSamples(frames_, audioPackets_)))
                {
                    break;
                }

                const bool hasVideo = !frames_.empty();
                const bool hasAudio = !audioPackets_.empty();
                writeVideo = hasVideo &&
                    (!hasAudio || frames_.front().sampleTimeHns <= audioPackets_.front().sampleTimeHns);

                if (writeVideo)
                {
                    videoFrame = std::move(frames_.front());
                    frames_.pop_front();
                }
                else
                {
                    audioPacket = std::move(audioPackets_.front());
                    audioPackets_.pop_front();
                }
            }

            if (writeVideo)
            {
                LONGLONG duration = 10000000LL / DefaultFrameRate;
                if (lastVideoSampleTime >= 0 && videoFrame.sampleTimeHns > lastVideoSampleTime)
                {
                    duration = videoFrame.sampleTimeHns - lastVideoSampleTime;
                }

                hr = WriteVideoFrameSample(
                    sinkWriter.Get(),
                    streamIndices.videoStreamIndex,
                    videoFrame,
                    duration);
                if (FAILED(hr))
                {
                    SetFailure(BuildMfError(L"写入视频帧失败。", hr));
                    break;
                }

                lastVideoSampleTime = videoFrame.sampleTimeHns;
            }
            else
            {
                hr = WriteAudioSample(
                    sinkWriter.Get(),
                    streamIndices.audioStreamIndex,
                    audioPacket);
                if (FAILED(hr))
                {
                    SetFailure(BuildMfError(L"写入音频样本失败。", hr));
                    break;
                }
            }
        }

        if (sinkWriter)
        {
            sinkWriter->Finalize();
        }

        if (mediaFoundationReady)
        {
            MFShutdown();
        }

        CoUninitialize();

        std::lock_guard<std::mutex> lock(mutex_);
        frames_.clear();
        audioPackets_.clear();
        started_ = false;
        stopping_ = false;
        startupCompleted_ = true;
        condition_.notify_all();
    }

    void PluginVideoRecordMfWriter::SetFailure(const std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        lastError_ = error;
        startupCompleted_ = true;
        condition_.notify_all();
    }
}
