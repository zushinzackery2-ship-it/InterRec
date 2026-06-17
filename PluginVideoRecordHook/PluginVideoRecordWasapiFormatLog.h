#pragma once

#include "PluginVideoRecordWasapiAudioConverter.h"

namespace PluginVideoRecord
{
    const char* GetWasapiRenderFormatSourceText(WasapiRenderFormatSource source);
    void LogWasapiSourceFormat(
        const char* scope,
        const WasapiSourceFormat& format,
        WasapiRenderFormatSource source);
}
