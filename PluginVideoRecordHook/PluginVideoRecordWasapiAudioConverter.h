#pragma once

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    enum class WasapiRenderFormatSource : std::uint8_t
    {
        Unknown,
        AudioClientInitialize
    };

    struct WasapiSourceFormat
    {
        WORD formatTag;
        WORD channels;
        DWORD sampleRate;
        WORD bitsPerSample;
        WORD validBitsPerSample;
        WORD blockAlign;
        DWORD channelMask;
        bool isFloat;
        bool isPcm;
    };

    struct WasapiRenderBuffer
    {
        WasapiSourceFormat format;
        WasapiRenderFormatSource formatSource;
        UINT32 frameCount;
        DWORD flags;
        std::vector<std::uint8_t> bytes;
    };

    bool TryParseWasapiSourceFormat(const WAVEFORMATEX* waveFormat, WasapiSourceFormat& format);
    size_t GetWasapiRenderBufferByteCount(const WasapiSourceFormat& format, UINT32 frameCount);
    bool ConvertWasapiRenderBufferToCapturedPacket(
        const WasapiRenderBuffer& renderBuffer,
        LONGLONG sampleTimeHns,
        CapturedAudioPacket& packet);
}
