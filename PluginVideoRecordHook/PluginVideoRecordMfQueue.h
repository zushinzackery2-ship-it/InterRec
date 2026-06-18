#pragma once

namespace PluginVideoRecord
{
    struct CapturedFrame;
    struct CapturedAudioPacket;

    struct MfQueueBudget
    {
        size_t queuedBytes;
        size_t normalBudgetBytes;
        size_t currentBudgetBytes;
        size_t hardCapBytes;
        size_t droppedSamples;
        size_t rejectedSamples;
        size_t suppressedDropLogs;
        ULONGLONG lastDropLogTick;
    };

    void ResetVideoQueueBudget(MfQueueBudget& budget);
    void ResetAudioQueueBudget(MfQueueBudget& budget);

    bool EnqueueCapturedFrame(
        std::deque<CapturedFrame>& frames,
        MfQueueBudget& budget,
        CapturedFrame&& frame);

    bool CanAcceptCapturedFrame(
        const std::deque<CapturedFrame>& frames,
        MfQueueBudget& budget,
        size_t frameBytes);

    bool EnqueueCapturedAudioPacket(
        std::deque<CapturedAudioPacket>& packets,
        MfQueueBudget& budget,
        CapturedAudioPacket&& packet);

    void OnCapturedFrameDequeued(MfQueueBudget& budget, const CapturedFrame& frame);
    void OnCapturedAudioPacketDequeued(MfQueueBudget& budget, const CapturedAudioPacket& packet);
    void ClearCapturedFrameQueue(std::deque<CapturedFrame>& frames, MfQueueBudget& budget);
    void ClearCapturedAudioPacketQueue(std::deque<CapturedAudioPacket>& packets, MfQueueBudget& budget);
}
