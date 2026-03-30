#pragma once

#include "PluginVideoRecordMfQueue.h"

namespace PluginVideoRecord
{
    struct CapturedFrame
    {
        UINT width;
        UINT height;
        LONGLONG sampleTimeHns;
        std::vector<std::uint8_t> pixels;
    };

    struct CapturedAudioPacket
    {
        LONGLONG sampleTimeHns;
        LONGLONG durationHns;
        std::vector<std::uint8_t> samples;
    };

    class PluginVideoRecordMfWriter
    {
    public:
        PluginVideoRecordMfWriter();
        ~PluginVideoRecordMfWriter();

        bool Start(const std::wstring& outputPath, UINT width, UINT height, std::wstring& error);
        void Stop();
        bool EnqueueFrame(CapturedFrame&& frame);
        bool EnqueueAudioPacket(CapturedAudioPacket&& packet);
        bool TryGetLastError(std::wstring& error) const;

    private:
        void WorkerThread();
        void SetFailure(const std::wstring& error);

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<CapturedFrame> frames_;
        std::deque<CapturedAudioPacket> audioPackets_;
        MfQueueBudget videoQueueBudget_;
        MfQueueBudget audioQueueBudget_;
        std::thread workerThread_;
        std::wstring outputPath_;
        UINT width_;
        UINT height_;
        bool started_;
        bool stopping_;
        bool failed_;
        bool startupCompleted_;
        std::wstring lastError_;
    };
}
