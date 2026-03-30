#pragma once

#include "PluginVideoRecordMfQueue.h"

#include "PluginVideoRecordInternalLogger.h"

namespace PluginVideoRecord::MfQueueInternal
{
    constexpr size_t QueueBudgetAlignmentBytes = 4ull * 1024ull * 1024ull;
    constexpr size_t MinimumBudgetGrowthBytes = 16ull * 1024ull * 1024ull;
    constexpr double QueueGrowThresholdRatio = 0.75;
    constexpr double QueueShrinkThresholdRatio = 0.35;
    constexpr DWORDLONG PhysicalMemoryReserveBytes = 512ull * 1024ull * 1024ull;

    constexpr size_t VideoDefaultNormalBudgetBytes = 128ull * 1024ull * 1024ull;
    constexpr size_t VideoDefaultHardCapBytes = 512ull * 1024ull * 1024ull;
    constexpr size_t VideoAbsoluteHardCapBytes = 1024ull * 1024ull * 1024ull;
    constexpr size_t VideoNormalSampleMultiplier = 6;
    constexpr size_t VideoBurstSampleMultiplier = 24;

    constexpr size_t AudioDefaultNormalBudgetBytes = 16ull * 1024ull * 1024ull;
    constexpr size_t AudioDefaultHardCapBytes = 64ull * 1024ull * 1024ull;
    constexpr size_t AudioAbsoluteHardCapBytes = 256ull * 1024ull * 1024ull;
    constexpr size_t AudioNormalSampleMultiplier = 256;
    constexpr size_t AudioBurstSampleMultiplier = 1024;

    inline size_t AlignUpBytes(size_t value, size_t alignment)
    {
        if (alignment == 0)
        {
            return value;
        }

        const size_t remainder = value % alignment;
        if (remainder == 0)
        {
            return value;
        }

        return value + (alignment - remainder);
    }

    inline size_t SaturatingMultiply(size_t value, size_t multiplier)
    {
        if (value == 0 || multiplier == 0)
        {
            return 0;
        }

        const size_t maxValue = std::numeric_limits<size_t>::max();
        if (value > maxValue / multiplier)
        {
            return maxValue;
        }

        return value * multiplier;
    }

    inline size_t ClampBudget(size_t value, size_t minimumValue, size_t maximumValue)
    {
        return std::max(minimumValue, std::min(value, maximumValue));
    }

    inline bool QueryAvailablePhysicalBytes(DWORDLONG& availablePhysicalBytes)
    {
        MEMORYSTATUSEX memoryStatus = {};
        memoryStatus.dwLength = sizeof(memoryStatus);
        if (!GlobalMemoryStatusEx(&memoryStatus))
        {
            return false;
        }

        availablePhysicalBytes = memoryStatus.ullAvailPhys;
        return true;
    }

    inline void ResetBudget(
        PluginVideoRecord::MfQueueBudget& budget,
        size_t normalBudgetBytes,
        size_t hardCapBytes)
    {
        budget.queuedBytes = 0;
        budget.normalBudgetBytes = normalBudgetBytes;
        budget.currentBudgetBytes = normalBudgetBytes;
        budget.hardCapBytes = hardCapBytes;
        budget.droppedSamples = 0;
    }

    inline void EnsureBudgetFloor(
        PluginVideoRecord::MfQueueBudget& budget,
        size_t sampleBytes,
        size_t defaultNormalBudgetBytes,
        size_t defaultHardCapBytes,
        size_t absoluteHardCapBytes,
        size_t normalSampleMultiplier,
        size_t burstSampleMultiplier)
    {
        const size_t sampleNormalBudget = AlignUpBytes(
            SaturatingMultiply(sampleBytes, normalSampleMultiplier),
            QueueBudgetAlignmentBytes);
        const size_t sampleHardCap = AlignUpBytes(
            SaturatingMultiply(sampleBytes, burstSampleMultiplier),
            QueueBudgetAlignmentBytes);

        budget.normalBudgetBytes = std::max(defaultNormalBudgetBytes, sampleNormalBudget);
        budget.hardCapBytes = ClampBudget(
            std::max(defaultHardCapBytes, sampleHardCap),
            budget.normalBudgetBytes,
            absoluteHardCapBytes);
        budget.currentBudgetBytes = ClampBudget(
            budget.currentBudgetBytes,
            budget.normalBudgetBytes,
            budget.hardCapBytes);
    }

    inline size_t GetElasticBudgetCeiling(const PluginVideoRecord::MfQueueBudget& budget)
    {
        DWORDLONG availablePhysicalBytes = 0;
        if (!QueryAvailablePhysicalBytes(availablePhysicalBytes) ||
            availablePhysicalBytes <= PhysicalMemoryReserveBytes)
        {
            return budget.normalBudgetBytes;
        }

        const DWORDLONG elasticPhysicalBytes = availablePhysicalBytes - PhysicalMemoryReserveBytes;
        const size_t elasticBudget = AlignUpBytes(
            static_cast<size_t>(elasticPhysicalBytes / 8ull),
            QueueBudgetAlignmentBytes);
        return ClampBudget(elasticBudget, budget.normalBudgetBytes, budget.hardCapBytes);
    }

    inline bool TryGrowBudget(
        PluginVideoRecord::MfQueueBudget& budget,
        size_t requiredBytes,
        const char* queueName)
    {
        const size_t ceilingBytes = GetElasticBudgetCeiling(budget);
        if (requiredBytes > ceilingBytes)
        {
            return false;
        }

        size_t nextBudgetBytes = budget.currentBudgetBytes;
        while (nextBudgetBytes < requiredBytes)
        {
            const size_t growStepBytes = std::max(MinimumBudgetGrowthBytes, nextBudgetBytes / 2);
            const size_t proposedBudgetBytes = nextBudgetBytes + growStepBytes;
            nextBudgetBytes = std::min(ceilingBytes, std::max(requiredBytes, proposedBudgetBytes));
        }

        if (nextBudgetBytes > budget.currentBudgetBytes)
        {
            PvrcInternalLogger::Log(
                "[PVRC][MfWriter] %s queue budget expanded: %zu MB -> %zu MB (queued=%zu MB)",
                queueName,
                budget.currentBudgetBytes / (1024ull * 1024ull),
                nextBudgetBytes / (1024ull * 1024ull),
                budget.queuedBytes / (1024ull * 1024ull));
            budget.currentBudgetBytes = nextBudgetBytes;
        }

        return true;
    }

    inline void MaybeShrinkBudget(
        PluginVideoRecord::MfQueueBudget& budget,
        const char* queueName)
    {
        if (budget.currentBudgetBytes <= budget.normalBudgetBytes)
        {
            return;
        }

        const size_t shrinkThresholdBytes = static_cast<size_t>(
            static_cast<double>(budget.currentBudgetBytes) * QueueShrinkThresholdRatio);
        if (budget.queuedBytes > shrinkThresholdBytes)
        {
            return;
        }

        const size_t desiredBudgetBytes = ClampBudget(
            AlignUpBytes(
                std::max(
                    budget.normalBudgetBytes,
                    SaturatingMultiply(std::max<size_t>(budget.queuedBytes, 1), 2)),
                QueueBudgetAlignmentBytes),
            budget.normalBudgetBytes,
            budget.currentBudgetBytes);
        if (desiredBudgetBytes >= budget.currentBudgetBytes)
        {
            return;
        }

        PvrcInternalLogger::Log(
            "[PVRC][MfWriter] %s queue budget shrunk: %zu MB -> %zu MB (queued=%zu MB)",
            queueName,
            budget.currentBudgetBytes / (1024ull * 1024ull),
            desiredBudgetBytes / (1024ull * 1024ull),
            budget.queuedBytes / (1024ull * 1024ull));
        budget.currentBudgetBytes = desiredBudgetBytes;
    }
}
