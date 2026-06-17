#include "pch.h"

#include "PluginVideoRecordInternalLogger.h"
#include "PluginVideoRecordWasapiFormatLog.h"

namespace PluginVideoRecord
{
    const char* GetWasapiRenderFormatSourceText(WasapiRenderFormatSource source)
    {
        switch (source)
        {
        case WasapiRenderFormatSource::AudioClientInitialize:
            return "audio-client";
        default:
            return "unknown";
        }
    }

    void LogWasapiSourceFormat(
        const char* scope,
        const WasapiSourceFormat& format,
        WasapiRenderFormatSource source)
    {
        PvrcInternalLogger::Log(
            "[PVRC][WasapiRender] %s format source=%s tag=%u channels=%u rate=%u bits=%u valid=%u block=%u mask=0x%08X float=%u pcm=%u",
            scope,
            GetWasapiRenderFormatSourceText(source),
            static_cast<unsigned>(format.formatTag),
            static_cast<unsigned>(format.channels),
            static_cast<unsigned>(format.sampleRate),
            static_cast<unsigned>(format.bitsPerSample),
            static_cast<unsigned>(format.validBitsPerSample),
            static_cast<unsigned>(format.blockAlign),
            static_cast<unsigned>(format.channelMask),
            format.isFloat ? 1u : 0u,
            format.isPcm ? 1u : 0u);
    }
}
