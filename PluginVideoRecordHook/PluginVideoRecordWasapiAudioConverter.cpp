#include "pch.h"

#include <ksmedia.h>
#include <mmreg.h>

#include "PluginVideoRecordWasapiAudioConverter.h"
#include "PluginVideoRecordWasapiSampleReader.h"

namespace
{
    bool IsFormatSubType(const WAVEFORMATEXTENSIBLE& format, const GUID& subtype)
    {
        return IsEqualGUID(format.SubFormat, subtype) != FALSE;
    }

    bool IsSilentBuffer(const PluginVideoRecord::WasapiRenderBuffer& renderBuffer)
    {
        return (renderBuffer.flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
    }

    void ReadStereoFrame(
        const PluginVideoRecord::WasapiSourceFormat& format,
        const std::uint8_t* data,
        UINT32 sourceFrameIndex,
        PluginVideoRecord::WasapiSampleReader::PcmValidBitsAlignment pcmAlignment,
        double& left,
        double& right)
    {
        const std::uint8_t* frame =
            data + static_cast<size_t>(sourceFrameIndex) * format.blockAlign;

        if (format.channels == 1)
        {
            left = PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                format,
                frame,
                0,
                pcmAlignment);
            right = left;
            return;
        }

        left = PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
            format,
            frame,
            0,
            pcmAlignment);
        right = PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
            format,
            frame,
            1,
            pcmAlignment);

        if (format.channels == 2)
        {
            return;
        }

        double leftWeight = 1.0;
        double rightWeight = 1.0;

        if (format.channels > 2)
        {
            const double center =
                PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                    format,
                    frame,
                    2,
                    pcmAlignment) * 0.70710678118;
            left += center;
            right += center;
            leftWeight += 0.70710678118;
            rightWeight += 0.70710678118;
        }

        if (format.channels > 3)
        {
            const double lfe =
                PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                    format,
                    frame,
                    3,
                    pcmAlignment) * 0.25;
            left += lfe;
            right += lfe;
            leftWeight += 0.25;
            rightWeight += 0.25;
        }

        if (format.channels > 4)
        {
            left +=
                PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                    format,
                    frame,
                    4,
                    pcmAlignment) * 0.70710678118;
            leftWeight += 0.70710678118;
        }

        if (format.channels > 5)
        {
            right +=
                PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                    format,
                    frame,
                    5,
                    pcmAlignment) * 0.70710678118;
            rightWeight += 0.70710678118;
        }

        for (WORD channelIndex = 6; channelIndex < format.channels; ++channelIndex)
        {
            const double value =
                PluginVideoRecord::WasapiSampleReader::ReadChannelSample(
                    format,
                    frame,
                    channelIndex,
                    pcmAlignment) * 0.5;
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
        PluginVideoRecord::WasapiSampleReader::PcmValidBitsAlignment pcmAlignment,
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
        ReadStereoFrame(
            renderBuffer.format,
            renderBuffer.bytes.data(),
            firstFrame,
            pcmAlignment,
            firstLeft,
            firstRight);
        ReadStereoFrame(
            renderBuffer.format,
            renderBuffer.bytes.data(),
            secondFrame,
            pcmAlignment,
            secondLeft,
            secondRight);

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

        if (format.validBitsPerSample == 0 || format.validBitsPerSample > format.bitsPerSample)
        {
            format.validBitsPerSample = format.bitsPerSample;
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

        const WasapiSampleReader::PcmValidBitsAlignment pcmAlignment =
            WasapiSampleReader::DetectPcmValidBitsAlignment(
                renderBuffer.format,
                renderBuffer.bytes.empty() ? nullptr : renderBuffer.bytes.data(),
                renderBuffer.frameCount);

        std::int16_t* destination = reinterpret_cast<std::int16_t*>(packet.samples.data());
        for (size_t outputFrameIndex = 0; outputFrameIndex < outputFrameCount; ++outputFrameIndex)
        {
            double left = 0.0;
            double right = 0.0;
            ReadResampledStereoFrame(
                renderBuffer,
                outputFrameIndex,
                pcmAlignment,
                left,
                right);

            destination[outputFrameIndex * 2] =
                WasapiSampleReader::FloatToInt16(left);
            destination[outputFrameIndex * 2 + 1] =
                WasapiSampleReader::FloatToInt16(right);
        }

        return true;
    }
}
