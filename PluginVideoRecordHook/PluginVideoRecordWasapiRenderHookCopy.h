#pragma once

#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    struct PendingRenderSnapshot
    {
        WasapiSourceFormat format = {};
        WasapiRenderFormatSource formatSource = WasapiRenderFormatSource::Unknown;
        BYTE* buffer = nullptr;
        UINT32 frameCount = 0;
        DWORD flags = 0;
        bool silent = false;
        bool valid = false;
    };

    PendingRenderSnapshot TakePendingRenderSnapshot(
        HookRuntime& runtime,
        IAudioRenderClient* renderClient,
        UINT32 frameCount,
        DWORD flags);

    bool CopyRenderBytesCached(
        const WasapiSourceFormat& format,
        BYTE* source,
        UINT32 frameCount,
        std::vector<std::uint8_t>& destination);
}
