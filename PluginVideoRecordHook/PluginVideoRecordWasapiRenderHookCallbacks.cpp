#include "pch.h"

#include "PluginVideoRecordWasapiRenderCapture.h"
#include "PluginVideoRecordWasapiRenderCaptureInternal.h"
#include "PluginVideoRecordWasapiRenderHookCopy.h"
#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace
{
    using namespace PluginVideoRecord::WasapiRenderHookInternal;

    AudioClientInitializeFn OriginalAudioClientInitialize()
    {
        return reinterpret_cast<AudioClientInitializeFn>(Runtime().audioClientInitializePatch.original);
    }

    AudioClientGetServiceFn OriginalAudioClientGetService()
    {
        return reinterpret_cast<AudioClientGetServiceFn>(Runtime().audioClientGetServicePatch.original);
    }

    AudioRenderClientGetBufferFn OriginalRenderClientGetBuffer()
    {
        return reinterpret_cast<AudioRenderClientGetBufferFn>(Runtime().renderClientGetBufferPatch.original);
    }

    AudioRenderClientReleaseBufferFn OriginalRenderClientReleaseBuffer()
    {
        return reinterpret_cast<AudioRenderClientReleaseBufferFn>(
            Runtime().renderClientReleaseBufferPatch.original);
    }

    UnknownReleaseFn OriginalAudioClientRelease()
    {
        return reinterpret_cast<UnknownReleaseFn>(Runtime().audioClientReleasePatch.original);
    }

    UnknownReleaseFn OriginalRenderClientRelease()
    {
        return reinterpret_cast<UnknownReleaseFn>(Runtime().renderClientReleasePatch.original);
    }

}

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    HRESULT STDMETHODCALLTYPE HookedAudioClientInitialize(
        IAudioClient* self,
        AUDCLNT_SHAREMODE shareMode,
        DWORD streamFlags,
        REFERENCE_TIME bufferDuration,
        REFERENCE_TIME periodicity,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid)
    {
        AudioClientInitializeFn original = OriginalAudioClientInitialize();
        if (!original)
        {
            return E_POINTER;
        }

        HRESULT hr = original(
            self,
            shareMode,
            streamFlags,
            bufferDuration,
            periodicity,
            format,
            audioSessionGuid);
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
        AudioClientState& state = runtime.audioClients[self];
        state.format = sourceFormat;
        state.formatReady = true;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE HookedAudioClientGetService(
        IAudioClient* self,
        REFIID serviceId,
        void** service)
    {
        AudioClientGetServiceFn original = OriginalAudioClientGetService();
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
        if (audioState == runtime.audioClients.end() &&
            runtime.defaultRenderFormatReady)
        {
            AudioClientState fallbackState = {};
            fallbackState.format = runtime.defaultRenderFormat;
            fallbackState.formatReady = true;
            runtime.audioClients[self] = fallbackState;
        }

        const auto adoptedAudioState = runtime.audioClients.find(self);
        if (adoptedAudioState == runtime.audioClients.end() || !adoptedAudioState->second.formatReady)
        {
            return hr;
        }

        auto* renderClient = static_cast<IAudioRenderClient*>(*service);
        RenderClientState& renderState = runtime.renderClients[renderClient];
        renderState.format = adoptedAudioState->second.format;
        renderState.pendingBuffer = nullptr;
        renderState.pendingFrameCount = 0;
        renderState.pending = false;
        return hr;
    }

    ULONG STDMETHODCALLTYPE HookedAudioClientRelease(IUnknown* self)
    {
        UnknownReleaseFn original = OriginalAudioClientRelease();
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

    HRESULT STDMETHODCALLTYPE HookedRenderClientGetBuffer(
        IAudioRenderClient* self,
        UINT32 frameCount,
        BYTE** data)
    {
        AudioRenderClientGetBufferFn original = OriginalRenderClientGetBuffer();
        if (!original)
        {
            return E_POINTER;
        }

        HRESULT hr = original(self, frameCount, data);
        if (FAILED(hr) || !self)
        {
            return hr;
        }

        HookRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        auto renderState = runtime.renderClients.find(self);
        if (renderState == runtime.renderClients.end() && runtime.defaultRenderFormatReady)
        {
            RenderClientState fallbackState = {};
            fallbackState.format = runtime.defaultRenderFormat;
            renderState = runtime.renderClients.emplace(self, fallbackState).first;
        }

        if (renderState == runtime.renderClients.end())
        {
            return hr;
        }

        renderState->second.pendingBuffer = data ? *data : nullptr;
        renderState->second.pendingFrameCount = frameCount;
        renderState->second.pending = data && *data && frameCount > 0;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE HookedRenderClientReleaseBuffer(
        IAudioRenderClient* self,
        UINT32 frameCount,
        DWORD flags)
    {
        AudioRenderClientReleaseBufferFn original = OriginalRenderClientReleaseBuffer();
        if (!original)
        {
            return E_POINTER;
        }

        WasapiRenderBuffer renderBuffer = {};
        bool hasBuffer = false;

        if (self &&
            frameCount > 0 &&
            PluginVideoRecord::WasapiRenderCaptureInternal::IsAcceptingRenderBuffersFast())
        {
            HookRuntime& runtime = Runtime();
            const PendingRenderSnapshot snapshot =
                TakePendingRenderSnapshot(runtime, self, frameCount, flags);
            if (snapshot.valid)
            {
                renderBuffer.format = snapshot.format;
                renderBuffer.frameCount = snapshot.frameCount;
                renderBuffer.flags = snapshot.flags;
                if (snapshot.silent)
                {
                    hasBuffer = true;
                }
                else
                {
                    hasBuffer = CopyRenderBytesCached(
                        snapshot.format,
                        snapshot.buffer,
                        snapshot.frameCount,
                        renderBuffer.bytes);
                }
            }
        }

        HRESULT hr = original(self, frameCount, flags);
        if (SUCCEEDED(hr) && hasBuffer)
        {
            PluginVideoRecordWasapiRenderCapture::SubmitRenderBuffer(std::move(renderBuffer));
        }

        return hr;
    }

    ULONG STDMETHODCALLTYPE HookedRenderClientRelease(IUnknown* self)
    {
        UnknownReleaseFn original = OriginalRenderClientRelease();
        if (!original)
        {
            return 0;
        }

        ULONG result = original(self);
        if (result == 0 && self)
        {
            HookRuntime& runtime = Runtime();
            std::lock_guard<std::mutex> lock(runtime.mutex);
            runtime.renderClients.erase(reinterpret_cast<IAudioRenderClient*>(self));
        }

        return result;
    }
}
