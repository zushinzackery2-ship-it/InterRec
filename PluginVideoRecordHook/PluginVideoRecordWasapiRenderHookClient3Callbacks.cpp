#include "pch.h"

#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace
{
    using namespace PluginVideoRecord::WasapiRenderHookInternal;

    AudioClient3InitializeSharedAudioStreamFn OriginalAudioClient3InitializeSharedAudioStream()
    {
        return reinterpret_cast<AudioClient3InitializeSharedAudioStreamFn>(
            Runtime().audioClient3InitializeSharedAudioStreamPatch.original);
    }

    AudioClientGetServiceFn OriginalAudioClient3GetService()
    {
        return reinterpret_cast<AudioClientGetServiceFn>(Runtime().audioClient3GetServicePatch.original);
    }

    UnknownReleaseFn OriginalAudioClient3Release()
    {
        return reinterpret_cast<UnknownReleaseFn>(Runtime().audioClient3ReleasePatch.original);
    }
}

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    HRESULT STDMETHODCALLTYPE HookedAudioClient3InitializeSharedAudioStream(
        IAudioClient3* self,
        DWORD streamFlags,
        UINT32 periodInFrames,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid)
    {
        AudioClient3InitializeSharedAudioStreamFn original =
            OriginalAudioClient3InitializeSharedAudioStream();
        if (!original)
        {
            return E_POINTER;
        }

        HRESULT hr = original(self, streamFlags, periodInFrames, format, audioSessionGuid);
        if (FAILED(hr) || !self || !format || (streamFlags & AUDCLNT_STREAMFLAGS_LOOPBACK) != 0)
        {
            return hr;
        }

        WasapiSourceFormat sourceFormat = {};
        if (!TryParseWasapiSourceFormat(format, sourceFormat))
        {
            return hr;
        }

        HookRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        AudioClientState& state = runtime.audioClients[reinterpret_cast<IAudioClient*>(self)];
        state.format = sourceFormat;
        state.formatReady = true;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE HookedAudioClient3GetService(
        IAudioClient* self,
        REFIID serviceId,
        void** service)
    {
        AudioClientGetServiceFn original = OriginalAudioClient3GetService();
        if (!original)
        {
            return E_POINTER;
        }

        HRESULT hr = original(self, serviceId, service);
        if (FAILED(hr) ||
            !self ||
            !service ||
            !*service ||
            IsEqualIID(serviceId, __uuidof(IAudioRenderClient)) == FALSE)
        {
            return hr;
        }

        HookRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        const auto audioState = runtime.audioClients.find(self);
        if (audioState == runtime.audioClients.end() || !audioState->second.formatReady)
        {
            return hr;
        }

        auto* renderClient = static_cast<IAudioRenderClient*>(*service);
        RenderClientState& renderState = runtime.renderClients[renderClient];
        renderState.format = audioState->second.format;
        renderState.pendingBuffer = nullptr;
        renderState.pendingFrameCount = 0;
        renderState.pending = false;
        return hr;
    }

    ULONG STDMETHODCALLTYPE HookedAudioClient3Release(IUnknown* self)
    {
        UnknownReleaseFn original = OriginalAudioClient3Release();
        if (!original)
        {
            return 0;
        }

        ULONG result = original(self);
        if (result == 0 && self)
        {
            HookRuntime& runtime = Runtime();
            std::lock_guard<std::mutex> lock(runtime.mutex);
            runtime.audioClients.erase(reinterpret_cast<IAudioClient*>(self));
        }

        return result;
    }
}
