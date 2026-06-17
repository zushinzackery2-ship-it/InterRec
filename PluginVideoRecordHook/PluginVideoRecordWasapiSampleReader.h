#pragma once

#include "PluginVideoRecordWasapiAudioConverter.h"

namespace PluginVideoRecord::WasapiSampleReader
{
    enum class PcmValidBitsAlignment : std::uint8_t
    {
        Native,
        LeftAligned,
        RightAligned
    };

    double ClampUnit(double value);
    std::int16_t FloatToInt16(double value);
    PcmValidBitsAlignment DetectPcmValidBitsAlignment(
        const WasapiSourceFormat& format,
        const std::uint8_t* data,
        UINT32 frameCount);
    double ReadChannelSample(
        const WasapiSourceFormat& format,
        const std::uint8_t* frame,
        WORD channelIndex,
        PcmValidBitsAlignment alignment);
}
