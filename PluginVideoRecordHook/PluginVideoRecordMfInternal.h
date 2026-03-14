#pragma once

namespace PluginVideoRecord
{
    HRESULT ConfigureVideoType(IMFMediaType* mediaType, const GUID& subtype, UINT width, UINT height);
    HRESULT ConfigurePcmAudioType(IMFMediaType* mediaType);
    HRESULT ConfigureAacAudioType(IMFMediaType* mediaType);
}
