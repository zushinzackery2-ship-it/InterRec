#include "pch.h"

#include <cmath>
#include <limits>

#include "PluginVideoRecordWasapiSampleReader.h"

namespace
{
    constexpr double PcmClampMin = -1.0;
    constexpr double PcmClampMax = 1.0;
    constexpr UINT32 MaxAlignmentDetectFrames = 128;

    std::uint64_t ReadUnsignedLittleEndian(const std::uint8_t* source, WORD byteCount)
    {
        std::uint64_t value = 0;
        const WORD safeByteCount = std::min<WORD>(byteCount, 8);
        for (WORD byteIndex = 0; byteIndex < safeByteCount; ++byteIndex)
        {
            value |= static_cast<std::uint64_t>(source[byteIndex]) << (byteIndex * 8);
        }

        return value;
    }

    std::uint64_t MaskForBits(WORD bitCount)
    {
        if (bitCount >= 64)
        {
            return ~0ull;
        }

        return (1ull << bitCount) - 1ull;
    }

    std::int64_t SignExtend(std::uint64_t value, WORD bitCount)
    {
        if (bitCount == 0)
        {
            return 0;
        }

        if (bitCount >= 64)
        {
            std::int64_t signedValue = 0;
            CopyMemory(&signedValue, &value, sizeof(signedValue));
            return signedValue;
        }

        const std::uint64_t signBit = 1ull << (bitCount - 1);
        const std::uint64_t masked = value & MaskForBits(bitCount);
        if ((masked & signBit) == 0)
        {
            return static_cast<std::int64_t>(masked);
        }

        const std::uint64_t magnitude = ((~masked) & MaskForBits(bitCount)) + 1ull;
        return -static_cast<std::int64_t>(magnitude);
    }

    double ReadIntegerSample(
        const PluginVideoRecord::WasapiSourceFormat& format,
        const std::uint8_t* sample,
        PluginVideoRecord::WasapiSampleReader::PcmValidBitsAlignment alignment)
    {
        const WORD containerBits = format.bitsPerSample;
        const WORD validBits = format.validBitsPerSample ? format.validBitsPerSample : containerBits;
        if (validBits == 0 || validBits > containerBits)
        {
            return 0.0;
        }

        const WORD bytesPerSample = static_cast<WORD>(containerBits / 8);
        const std::uint64_t rawValue = ReadUnsignedLittleEndian(sample, bytesPerSample);
        std::uint64_t alignedValue = rawValue;

        if (containerBits > validBits)
        {
            const WORD paddingBits = static_cast<WORD>(containerBits - validBits);
            if (alignment == PluginVideoRecord::WasapiSampleReader::PcmValidBitsAlignment::LeftAligned)
            {
                alignedValue = rawValue >> paddingBits;
            }
            else if (alignment == PluginVideoRecord::WasapiSampleReader::PcmValidBitsAlignment::RightAligned)
            {
                alignedValue = rawValue & MaskForBits(validBits);
            }
        }

        const std::int64_t signedValue = SignExtend(alignedValue, validBits);
        const double scale = std::ldexp(1.0, validBits - 1);
        return PluginVideoRecord::WasapiSampleReader::ClampUnit(
            static_cast<double>(signedValue) / scale);
    }

    bool HasRightAlignedSignExtension(
        std::uint64_t rawValue,
        WORD validBits,
        WORD containerBits)
    {
        if (containerBits <= validBits || containerBits >= 64)
        {
            return true;
        }

        const std::uint64_t highMask = MaskForBits(containerBits) & ~MaskForBits(validBits);
        const bool negative = (rawValue & (1ull << (validBits - 1))) != 0;
        const std::uint64_t expectedHighBits = negative ? highMask : 0ull;
        return (rawValue & highMask) == expectedHighBits;
    }
}

namespace PluginVideoRecord::WasapiSampleReader
{
    double ClampUnit(double value)
    {
        return std::max(PcmClampMin, std::min(PcmClampMax, value));
    }

    std::int16_t FloatToInt16(double value)
    {
        const double clamped = ClampUnit(value);
        const double scaled = clamped * static_cast<double>(std::numeric_limits<std::int16_t>::max());
        return static_cast<std::int16_t>(std::lround(scaled));
    }

    PcmValidBitsAlignment DetectPcmValidBitsAlignment(
        const WasapiSourceFormat& format,
        const std::uint8_t* data,
        UINT32 frameCount)
    {
        if (!data ||
            !format.isPcm ||
            format.isFloat ||
            format.channels == 0 ||
            format.bitsPerSample <= format.validBitsPerSample ||
            format.validBitsPerSample == 0)
        {
            return PcmValidBitsAlignment::Native;
        }

        const WORD containerBits = format.bitsPerSample;
        const WORD validBits = format.validBitsPerSample;
        if (validBits >= containerBits || containerBits > 64)
        {
            return PcmValidBitsAlignment::Native;
        }

        const WORD bytesPerSample = static_cast<WORD>(containerBits / 8);
        const WORD paddingBits = static_cast<WORD>(containerBits - validBits);
        const std::uint64_t lowPaddingMask = MaskForBits(paddingBits);
        const UINT32 framesToScan = std::min(frameCount, MaxAlignmentDetectFrames);
        size_t lowPaddingNonZeroCount = 0;
        size_t rightSignExtensionMismatchCount = 0;

        for (UINT32 frameIndex = 0; frameIndex < framesToScan; ++frameIndex)
        {
            const std::uint8_t* frame =
                data + static_cast<size_t>(frameIndex) * format.blockAlign;
            for (WORD channelIndex = 0; channelIndex < format.channels; ++channelIndex)
            {
                const std::uint8_t* sample =
                    frame + static_cast<size_t>(channelIndex) * bytesPerSample;
                const std::uint64_t rawValue = ReadUnsignedLittleEndian(sample, bytesPerSample);
                if ((rawValue & lowPaddingMask) != 0)
                {
                    ++lowPaddingNonZeroCount;
                }

                if (!HasRightAlignedSignExtension(rawValue, validBits, containerBits))
                {
                    ++rightSignExtensionMismatchCount;
                }
            }
        }

        if (lowPaddingNonZeroCount > 0)
        {
            return PcmValidBitsAlignment::RightAligned;
        }

        if (rightSignExtensionMismatchCount > 0)
        {
            return PcmValidBitsAlignment::LeftAligned;
        }

        return PcmValidBitsAlignment::LeftAligned;
    }

    double ReadChannelSample(
        const WasapiSourceFormat& format,
        const std::uint8_t* frame,
        WORD channelIndex,
        PcmValidBitsAlignment alignment)
    {
        const WORD bytesPerSample = static_cast<WORD>(format.bitsPerSample / 8);
        const std::uint8_t* sample = frame + static_cast<size_t>(channelIndex) * bytesPerSample;

        if (format.isFloat)
        {
            if (format.bitsPerSample == 32)
            {
                float value = 0.0f;
                CopyMemory(&value, sample, sizeof(value));
                return ClampUnit(static_cast<double>(value));
            }

            if (format.bitsPerSample == 64)
            {
                double value = 0.0;
                CopyMemory(&value, sample, sizeof(value));
                return ClampUnit(value);
            }

            return 0.0;
        }

        if (!format.isPcm)
        {
            return 0.0;
        }

        if (format.bitsPerSample == 8)
        {
            return (static_cast<int>(sample[0]) - 128) / 128.0;
        }

        return ReadIntegerSample(format, sample, alignment);
    }
}
