#include "pch.h"

#include "PluginVideoRecordWasapiRenderCapture.h"
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

    bool CopyRenderBytes(
        const PluginVideoRecord::WasapiSourceFormat& format,
        BYTE* source,
        UINT32 frameCount,
        std::vector<std::uint8_t>& destination)
    {
        const size_t byteCount =
            PluginVideoRecord::GetWasapiRenderBufferByteCount(format, frameCount);
        if (byteCount == 0)
        {
            return false;
        }

        destination.resize(byteCount);
        return TryCopyMemory(destination.data(), source, byteCount);
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

        if (self && frameCount > 0)
        {
            HookRuntime& runtime = Runtime();
            std::lock_guard<std::mutex> lock(runtime.mutex);
            auto renderState = runtime.renderClients.find(self);
            if (renderState != runtime.renderClients.end() && renderState->second.pending)
            {
                const UINT32 copyFrameCount = std::min(frameCount, renderState->second.pendingFrameCount);
                renderBuffer.format = renderState->second.format;
                renderBuffer.frameCount = copyFrameCount;
                renderBuffer.flags = flags;
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0)
                {
                    hasBuffer = copyFrameCount > 0;
                }
                else
                {
                    hasBuffer = CopyRenderBytes(
                        renderState->second.format,
                        renderState->second.pendingBuffer,
                        copyFrameCount,
                        renderBuffer.bytes);
                }

                renderState->second.pending = false;
                renderState->second.pendingBuffer = nullptr;
                renderState->second.pendingFrameCount = 0;
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
