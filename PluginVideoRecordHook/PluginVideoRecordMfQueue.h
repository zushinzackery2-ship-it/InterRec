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
    };

    void ResetVideoQueueBudget(MfQueueBudget& budget);
    void ResetAudioQueueBudget(MfQueueBudget& budget);

    bool EnqueueCapturedFrame(
        std::deque<CapturedFrame>& frames,
        MfQueueBudget& budget,
        CapturedFrame&& frame);

    bool EnqueueCapturedAudioPacket(
        std::deque<CapturedAudioPacket>& packets,
        MfQueueBudget& budget,
        CapturedAudioPacket&& packet);

    void OnCapturedFrameDequeued(MfQueueBudget& budget, const CapturedFrame& frame);
    void OnCapturedAudioPacketDequeued(MfQueueBudget& budget, const CapturedAudioPacket& packet);
}
