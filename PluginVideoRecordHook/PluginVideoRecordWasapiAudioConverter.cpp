#include "pch.h"

#include <cmath>
#include <ksmedia.h>
#include <limits>
#include <mmreg.h>

#include "PluginVideoRecordWasapiAudioConverter.h"

namespace
{
    constexpr double PcmClampMin = -1.0;
    constexpr double PcmClampMax = 1.0;

    bool IsFormatSubType(const WAVEFORMATEXTENSIBLE& format, const GUID& subtype)
    {
        return IsEqualGUID(format.SubFormat, subtype) != FALSE;
    }

    bool IsSilentBuffer(const PluginVideoRecord::WasapiRenderBuffer& renderBuffer)
    {
        return (renderBuffer.flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    }

    std::int32_t ReadSignedLittleEndian(const std::uint8_t* source, int byteCount)
    {
        std::int32_t value = 0;
        for (int byteIndex = 0; byteIndex < byteCount; ++byteIndex)
        {
            value |= static_cast<std::int32_t>(source[byteIndex]) << (byteIndex * 8);
        }

        const int shift = (4 - byteCount) * 8;
        return (value << shift) >> shift;
    }

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

    double ReadChannelSample(
        const PluginVideoRecord::WasapiSourceFormat& format,
        const std::uint8_t* frame,
        WORD channelIndex)
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

        switch (format.bitsPerSample)
        {
        case 8:
            return (static_cast<int>(sample[0]) - 128) / 128.0;

        case 16:
            return static_cast<double>(ReadSignedLittleEndian(sample, 2)) / 32768.0;

        case 24:
            return static_cast<double>(ReadSignedLittleEndian(sample, 3)) / 8388608.0;

        case 32:
            return static_cast<double>(ReadSignedLittleEndian(sample, 4)) / 2147483648.0;

        default:
            return 0.0;
        }
    }

    void ReadStereoFrame(
        const PluginVideoRecord::WasapiSourceFormat& format,
        const std::uint8_t* data,
        UINT32 sourceFrameIndex,
        double& left,
        double& right)
    {
        const std::uint8_t* frame =
            data + static_cast<size_t>(sourceFrameIndex) * format.blockAlign;

        if (format.channels == 1)
        {
            left = ReadChannelSample(format, frame, 0);
            right = left;
            return;
        }

        left = ReadChannelSample(format, frame, 0);
        right = ReadChannelSample(format, frame, 1);

        if (format.channels == 2)
        {
            return;
        }

        double leftWeight = 1.0;
        double rightWeight = 1.0;

        if (format.channels > 2)
        {
            const double center = ReadChannelSample(format, frame, 2) * 0.70710678118;
            left += center;
            right += center;
            leftWeight += 0.70710678118;
            rightWeight += 0.70710678118;
        }

        if (format.channels > 3)
        {
            const double lfe = ReadChannelSample(format, frame, 3) * 0.25;
            left += lfe;
            right += lfe;
            leftWeight += 0.25;
            rightWeight += 0.25;
        }

        if (format.channels > 4)
        {
            left += ReadChannelSample(format, frame, 4) * 0.70710678118;
            leftWeight += 0.70710678118;
        }

        if (format.channels > 5)
        {
            right += ReadChannelSample(format, frame, 5) * 0.70710678118;
            rightWeight += 0.70710678118;
        }

        for (WORD channelIndex = 6; channelIndex < format.channels; ++channelIndex)
        {
            const double value = ReadChannelSample(format, frame, channelIndex) * 0.5;
            if ((channelIndex & 1) == 0)
            {
                left += value;
                leftWeight += 0.5;
            }
            else
            {
                right += value;
                rightWeight += 0.5;
            }
        }

        left /= leftWeight;
        right /= rightWeight;
    }

    void ReadResampledStereoFrame(
        const PluginVideoRecord::WasapiRenderBuffer& renderBuffer,
        size_t outputFrameIndex,
        double& left,
        double& right)
    {
        if (IsSilentBuffer(renderBuffer) || renderBuffer.bytes.empty())
        {
            left = 0.0;
            right = 0.0;
            return;
        }

        const double sourcePosition =
            static_cast<double>(outputFrameIndex) *
            static_cast<double>(renderBuffer.format.sampleRate) /
            static_cast<double>(PluginVideoRecord::DefaultAudioSampleRate);

        const UINT32 firstFrame = std::min(
            static_cast<UINT32>(sourcePosition),
            renderBuffer.frameCount - 1);
        const UINT32 secondFrame = std::min(firstFrame + 1, renderBuffer.frameCount - 1);
        const double blend = sourcePosition - static_cast<double>(firstFrame);

        double firstLeft = 0.0;
        double firstRight = 0.0;
        double secondLeft = 0.0;
        double secondRight = 0.0;
        ReadStereoFrame(renderBuffer.format, renderBuffer.bytes.data(), firstFrame, firstLeft, firstRight);
        ReadStereoFrame(renderBuffer.format, renderBuffer.bytes.data(), secondFrame, secondLeft, secondRight);

        left = firstLeft + (secondLeft - firstLeft) * blend;
        right = firstRight + (secondRight - firstRight) * blend;
    }
}

namespace PluginVideoRecord
{
    bool TryParseWasapiSourceFormat(const WAVEFORMATEX* waveFormat, WasapiSourceFormat& format)
    {
        if (!waveFormat ||
            waveFormat->nChannels == 0 ||
            waveFormat->nSamplesPerSec == 0 ||
            waveFormat->wBitsPerSample == 0 ||
            waveFormat->nBlockAlign == 0 ||
            (waveFormat->wBitsPerSample % 8) != 0)
        {
            return false;
        }

        ZeroMemory(&format, sizeof(format));
        format.formatTag = waveFormat->wFormatTag;
        format.channels = waveFormat->nChannels;
        format.sampleRate = waveFormat->nSamplesPerSec;
        format.bitsPerSample = waveFormat->wBitsPerSample;
        format.validBitsPerSample = waveFormat->wBitsPerSample;
        format.blockAlign = waveFormat->nBlockAlign;
        format.channelMask = 0;

        if (waveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            if (waveFormat->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            {
                return false;
            }

            const WAVEFORMATEXTENSIBLE* extensible =
                reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(waveFormat);
            format.validBitsPerSample = extensible->Samples.wValidBitsPerSample
                ? extensible->Samples.wValidBitsPerSample
                : waveFormat->wBitsPerSample;
            format.channelMask = extensible->dwChannelMask;
            format.isPcm = IsFormatSubType(*extensible, KSDATAFORMAT_SUBTYPE_PCM);
            format.isFloat = IsFormatSubType(*extensible, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }
        else
        {
            format.isPcm = waveFormat->wFormatTag == WAVE_FORMAT_PCM;
            format.isFloat = waveFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        }

        if (!format.isPcm && !format.isFloat)
        {
            return false;
        }

        if (format.bitsPerSample != 8 &&
            format.bitsPerSample != 16 &&
            format.bitsPerSample != 24 &&
            format.bitsPerSample != 32 &&
            format.bitsPerSample != 64)
        {
            return false;
        }

        const WORD expectedMinimumBlockAlign =
            static_cast<WORD>(format.channels * (format.bitsPerSample / 8));
        return format.blockAlign >= expectedMinimumBlockAlign;
    }

    size_t GetWasapiRenderBufferByteCount(const WasapiSourceFormat& format, UINT32 frameCount)
    {
        return static_cast<size_t>(frameCount) * format.blockAlign;
    }

    bool ConvertWasapiRenderBufferToCapturedPacket(
        const WasapiRenderBuffer& renderBuffer,
        LONGLONG sampleTimeHns,
        CapturedAudioPacket& packet)
    {
        if (renderBuffer.frameCount == 0 || renderBuffer.format.sampleRate == 0)
        {
            return false;
        }

        const size_t requiredBytes =
            GetWasapiRenderBufferByteCount(renderBuffer.format, renderBuffer.frameCount);
        if (!IsSilentBuffer(renderBuffer) && renderBuffer.bytes.size() < requiredBytes)
        {
            return false;
        }

        const size_t outputFrameCount = std::max<size_t>(
            1,
            (static_cast<size_t>(renderBuffer.frameCount) * DefaultAudioSampleRate +
                renderBuffer.format.sampleRate / 2) /
                renderBuffer.format.sampleRate);
        const size_t outputBytes =
            outputFrameCount * DefaultAudioChannels * (DefaultAudioBitsPerSample / 8);

        packet.sampleTimeHns = sampleTimeHns;
        packet.durationHns =
            static_cast<LONGLONG>(outputFrameCount) * 10000000LL / DefaultAudioSampleRate;
        packet.samples.resize(outputBytes);

        std::int16_t* destination = reinterpret_cast<std::int16_t*>(packet.samples.data());
        for (size_t outputFrameIndex = 0; outputFrameIndex < outputFrameCount; ++outputFrameIndex)
        {
            double left = 0.0;
            double right = 0.0;
            ReadResampledStereoFrame(renderBuffer, outputFrameIndex, left, right);

            destination[outputFrameIndex * 2] = FloatToInt16(left);
            destination[outputFrameIndex * 2 + 1] = FloatToInt16(right);
        }

        return true;
    }
}
