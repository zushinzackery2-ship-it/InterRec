#include "pch.h"

#include "PluginVideoRecordMfQueueInternal.h"
#include "PluginVideoRecordMfQueue.h"
#include "PluginVideoRecordMfWriter.h"

namespace
{
    constexpr ULONGLONG QueuePressureLogIntervalMs = 1000;

    size_t GetFrameBytes(const PluginVideoRecord::CapturedFrame& frame)
    {
        return std::max<size_t>(frame.pixels.size(), 1);
    }

    size_t GetAudioPacketBytes(const PluginVideoRecord::CapturedAudioPacket& packet)
    {
        return std::max<size_t>(packet.samples.size(), 1);
    }

    template <typename SampleT, typename ByteGetterFn>
    void DropQueuedSamples(
        std::deque<SampleT>& queue,
        PluginVideoRecord::MfQueueBudget& budget,
        size_t& requiredBytes,
        size_t sampleBytes,
        ByteGetterFn byteGetter,
        size_t& droppedSamples,
        size_t& droppedBytes)
    {
        while (!queue.empty() && requiredBytes > budget.currentBudgetBytes)
        {
            const size_t droppedSampleBytes = byteGetter(queue.front());
            droppedBytes += droppedSampleBytes;
            budget.queuedBytes = droppedSampleBytes >= budget.queuedBytes
                ? 0
                : budget.queuedBytes - droppedSampleBytes;
            queue.pop_front();
            ++droppedSamples;
            requiredBytes = budget.queuedBytes + sampleBytes;
        }
    }

    void LogDroppedSamples(
        PluginVideoRecord::MfQueueBudget& budget,
        const char* queueName,
        size_t droppedSamples,
        size_t droppedBytes)
    {
        if (droppedSamples == 0)
        {
            return;
        }

        budget.droppedSamples += droppedSamples;

        const ULONGLONG now = GetTickCount64();
        if (budget.lastDropLogTick != 0 &&
            now - budget.lastDropLogTick < QueuePressureLogIntervalMs)
        {
            budget.suppressedDropLogs += droppedSamples;
            return;
        }

        PvrcInternalLogger::Log(
            "[PVRC][MfWriter] %s queue realtime drop: dropped=%zu suppressed=%zu totalDropped=%zu droppedBytes=%zu MB queued=%zu MB budget=%zu MB",
            queueName,
            droppedSamples,
            budget.suppressedDropLogs,
            budget.droppedSamples,
            droppedBytes / (1024ull * 1024ull),
            budget.queuedBytes / (1024ull * 1024ull),
            budget.currentBudgetBytes / (1024ull * 1024ull));
        budget.suppressedDropLogs = 0;
        budget.lastDropLogTick = now;
    }

    void RecordSkippedSample(PluginVideoRecord::MfQueueBudget& budget)
    {
        ++budget.skippedSamples;
    }

    template <typename SampleT, typename ByteGetterFn>
    bool EnqueueSample(
        std::deque<SampleT>& queue,
        PluginVideoRecord::MfQueueBudget& budget,
        SampleT&& sample,
        ByteGetterFn byteGetter,
        const char* queueName,
        size_t defaultNormalBudgetBytes,
        size_t defaultHardCapBytes,
        size_t absoluteHardCapBytes,
        size_t normalSampleMultiplier,
        size_t burstSampleMultiplier,
        bool allowElasticGrowth)
    {
        const size_t sampleBytes = byteGetter(sample);
        PluginVideoRecord::MfQueueInternal::EnsureBudgetFloor(
            budget,
            sampleBytes,
            defaultNormalBudgetBytes,
            defaultHardCapBytes,
            absoluteHardCapBytes,
            normalSampleMultiplier,
            burstSampleMultiplier);

        size_t requiredBytes = budget.queuedBytes + sampleBytes;
        if (allowElasticGrowth)
        {
            const size_t growThresholdBytes = static_cast<size_t>(
                static_cast<double>(budget.currentBudgetBytes) *
                PluginVideoRecord::MfQueueInternal::QueueGrowThresholdRatio);
            if (requiredBytes >= growThresholdBytes)
            {
                const size_t burstTargetBytes = requiredBytes + std::max(
                    sampleBytes,
                    PluginVideoRecord::MfQueueInternal::MinimumBudgetGrowthBytes);
                PluginVideoRecord::MfQueueInternal::TryGrowBudget(budget, burstTargetBytes, queueName);
            }

            if (requiredBytes > budget.currentBudgetBytes)
            {
                PluginVideoRecord::MfQueueInternal::TryGrowBudget(budget, requiredBytes, queueName);
            }
        }

        size_t droppedSamples = 0;
        size_t droppedBytes = 0;
        DropQueuedSamples(
            queue,
            budget,
            requiredBytes,
            sampleBytes,
            byteGetter,
            droppedSamples,
            droppedBytes);
        LogDroppedSamples(budget, queueName, droppedSamples, droppedBytes);

        if (requiredBytes > budget.currentBudgetBytes)
        {
            LogDroppedSamples(budget, queueName, 1, sampleBytes);
            return false;
        }

        budget.queuedBytes += sampleBytes;
        queue.emplace_back(std::move(sample));
        return true;
    }
}

namespace PluginVideoRecord
{
    void ResetVideoQueueBudget(MfQueueBudget& budget)
    {
        PluginVideoRecord::MfQueueInternal::ResetBudget(
            budget,
            PluginVideoRecord::MfQueueInternal::VideoDefaultNormalBudgetBytes,
            PluginVideoRecord::MfQueueInternal::VideoDefaultHardCapBytes);
    }

    void ResetAudioQueueBudget(MfQueueBudget& budget)
    {
        PluginVideoRecord::MfQueueInternal::ResetBudget(
            budget,
            PluginVideoRecord::MfQueueInternal::AudioDefaultNormalBudgetBytes,
            PluginVideoRecord::MfQueueInternal::AudioDefaultHardCapBytes);
    }

    bool EnqueueCapturedFrame(
        std::deque<CapturedFrame>& frames,
        MfQueueBudget& budget,
        CapturedFrame&& frame)
    {
        return EnqueueSample(
            frames,
            budget,
            std::move(frame),
            GetFrameBytes,
            "video",
            PluginVideoRecord::MfQueueInternal::VideoDefaultNormalBudgetBytes,
            PluginVideoRecord::MfQueueInternal::VideoDefaultHardCapBytes,
            PluginVideoRecord::MfQueueInternal::VideoAbsoluteHardCapBytes,
            PluginVideoRecord::MfQueueInternal::VideoNormalSampleMultiplier,
            PluginVideoRecord::MfQueueInternal::VideoBurstSampleMultiplier,
            false);
    }

    bool CanAcceptCapturedFrame(
        const std::deque<CapturedFrame>& frames,
        MfQueueBudget& budget,
        size_t frameBytes)
    {
        const size_t sampleBytes = std::max<size_t>(frameBytes, 1);
        PluginVideoRecord::MfQueueInternal::EnsureBudgetFloor(
            budget,
            sampleBytes,
            PluginVideoRecord::MfQueueInternal::VideoDefaultNormalBudgetBytes,
            PluginVideoRecord::MfQueueInternal::VideoDefaultHardCapBytes,
            PluginVideoRecord::MfQueueInternal::VideoAbsoluteHardCapBytes,
            PluginVideoRecord::MfQueueInternal::VideoNormalSampleMultiplier,
            PluginVideoRecord::MfQueueInternal::VideoBurstSampleMultiplier);

        if (frames.empty() || budget.queuedBytes + sampleBytes <= budget.currentBudgetBytes)
        {
            return true;
        }

        RecordSkippedSample(budget);
        return false;
    }

    bool EnqueueCapturedAudioPacket(
        std::deque<CapturedAudioPacket>& packets,
        MfQueueBudget& budget,
        CapturedAudioPacket&& packet)
    {
        return EnqueueSample(
            packets,
            budget,
            std::move(packet),
            GetAudioPacketBytes,
            "audio",
            PluginVideoRecord::MfQueueInternal::AudioDefaultNormalBudgetBytes,
            PluginVideoRecord::MfQueueInternal::AudioDefaultHardCapBytes,
            PluginVideoRecord::MfQueueInternal::AudioAbsoluteHardCapBytes,
            PluginVideoRecord::MfQueueInternal::AudioNormalSampleMultiplier,
            PluginVideoRecord::MfQueueInternal::AudioBurstSampleMultiplier,
            true);
    }

    void OnCapturedFrameDequeued(MfQueueBudget& budget, const CapturedFrame& frame)
    {
        const size_t sampleBytes = GetFrameBytes(frame);
        budget.queuedBytes = sampleBytes >= budget.queuedBytes ? 0 : budget.queuedBytes - sampleBytes;
        PluginVideoRecord::MfQueueInternal::MaybeShrinkBudget(budget, "video");
    }

    void OnCapturedAudioPacketDequeued(MfQueueBudget& budget, const CapturedAudioPacket& packet)
    {
        const size_t sampleBytes = GetAudioPacketBytes(packet);
        budget.queuedBytes = sampleBytes >= budget.queuedBytes ? 0 : budget.queuedBytes - sampleBytes;
        PluginVideoRecord::MfQueueInternal::MaybeShrinkBudget(budget, "audio");
    }

    void ClearCapturedFrameQueue(std::deque<CapturedFrame>& frames, MfQueueBudget& budget)
    {
        frames.clear();
        ResetVideoQueueBudget(budget);
    }

    void ClearCapturedAudioPacketQueue(std::deque<CapturedAudioPacket>& packets, MfQueueBudget& budget)
    {
        packets.clear();
        ResetAudioQueueBudget(budget);
    }
}
