#pragma once

#include <unordered_map>

#include "PluginVideoRecordWasapiAudioConverter.h"

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    constexpr size_t IUnknownReleaseSlot = 2;
    constexpr size_t AudioClientInitializeSlot = 3;
    constexpr size_t AudioClientGetServiceSlot = 14;
    constexpr size_t AudioClient3InitializeSharedAudioStreamSlot = 20;
    constexpr size_t AudioRenderClientGetBufferSlot = 3;
    constexpr size_t AudioRenderClientReleaseBufferSlot = 4;

    struct VtablePatch
    {
        void** slot = nullptr;
        void* original = nullptr;
        void* replacement = nullptr;
        bool installed = false;
    };

    struct AudioClientState
    {
        WasapiSourceFormat format = {};
        WasapiRenderFormatSource formatSource = WasapiRenderFormatSource::Unknown;
        bool formatReady = false;
    };

    struct RenderClientState
    {
        WasapiSourceFormat format = {};
        WasapiRenderFormatSource formatSource = WasapiRenderFormatSource::Unknown;
        BYTE* pendingBuffer = nullptr;
        UINT32 pendingFrameCount = 0;
        bool formatReady = false;
        bool pending = false;
    };

    struct HookRuntime
    {
        std::mutex mutex;
        VtablePatch audioClientInitializePatch;
        VtablePatch audioClientGetServicePatch;
        VtablePatch audioClientReleasePatch;
        VtablePatch audioClient3InitializeSharedAudioStreamPatch;
        VtablePatch audioClient3GetServicePatch;
        VtablePatch audioClient3ReleasePatch;
        VtablePatch renderClientGetBufferPatch;
        VtablePatch renderClientReleaseBufferPatch;
        VtablePatch renderClientReleasePatch;
        std::unordered_map<IAudioClient*, AudioClientState> audioClients;
        std::unordered_map<IAudioRenderClient*, RenderClientState> renderClients;
        bool installed = false;
    };

    using AudioClientInitializeFn = HRESULT(STDMETHODCALLTYPE*)(
        IAudioClient* self,
        AUDCLNT_SHAREMODE shareMode,
        DWORD streamFlags,
        REFERENCE_TIME bufferDuration,
        REFERENCE_TIME periodicity,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid);

    using AudioClientGetServiceFn = HRESULT(STDMETHODCALLTYPE*)(
        IAudioClient* self,
        REFIID serviceId,
        void** service);

    using AudioClient3InitializeSharedAudioStreamFn = HRESULT(STDMETHODCALLTYPE*)(
        IAudioClient3* self,
        DWORD streamFlags,
        UINT32 periodInFrames,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid);

    using AudioRenderClientGetBufferFn = HRESULT(STDMETHODCALLTYPE*)(
        IAudioRenderClient* self,
        UINT32 frameCount,
        BYTE** data);

    using AudioRenderClientReleaseBufferFn = HRESULT(STDMETHODCALLTYPE*)(
        IAudioRenderClient* self,
        UINT32 frameCount,
        DWORD flags);

    using UnknownReleaseFn = ULONG(STDMETHODCALLTYPE*)(IUnknown* self);

    HookRuntime& Runtime();
    std::wstring BuildHresultText(const wchar_t* text, HRESULT hr);
    std::wstring BuildWin32Text(const wchar_t* text, DWORD error);
    bool TryCopyMemory(void* destination, const void* source, size_t size);
    bool TryCopyMemoryCached(
        void* destination,
        const void* source,
        size_t size,
        const void*& cachedAddress,
        size_t& cachedBytes,
        bool& cachedValid);
    bool TryGetVtable(void* object, void*** vtable);
    bool PatchVtableSlot(
        void** vtable,
        size_t slotIndex,
        void* replacement,
        VtablePatch& patch,
        std::wstring& error);
    void RestoreVtablePatch(VtablePatch& patch);
    bool PatchAudioClient3VtableFromProbe(IAudioClient* audioClient, std::wstring& error);
    void RestoreAudioClient3Patches(HookRuntime& runtime);

    HRESULT STDMETHODCALLTYPE HookedAudioClientInitialize(
        IAudioClient* self,
        AUDCLNT_SHAREMODE shareMode,
        DWORD streamFlags,
        REFERENCE_TIME bufferDuration,
        REFERENCE_TIME periodicity,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid);
    HRESULT STDMETHODCALLTYPE HookedAudioClientGetService(
        IAudioClient* self,
        REFIID serviceId,
        void** service);
    ULONG STDMETHODCALLTYPE HookedAudioClientRelease(IUnknown* self);
    HRESULT STDMETHODCALLTYPE HookedAudioClient3InitializeSharedAudioStream(
        IAudioClient3* self,
        DWORD streamFlags,
        UINT32 periodInFrames,
        const WAVEFORMATEX* format,
        LPCGUID audioSessionGuid);
    HRESULT STDMETHODCALLTYPE HookedAudioClient3GetService(
        IAudioClient* self,
        REFIID serviceId,
        void** service);
    ULONG STDMETHODCALLTYPE HookedAudioClient3Release(IUnknown* self);
    HRESULT STDMETHODCALLTYPE HookedRenderClientGetBuffer(
        IAudioRenderClient* self,
        UINT32 frameCount,
        BYTE** data);
    HRESULT STDMETHODCALLTYPE HookedRenderClientReleaseBuffer(
        IAudioRenderClient* self,
        UINT32 frameCount,
        DWORD flags);
    ULONG STDMETHODCALLTYPE HookedRenderClientRelease(IUnknown* self);
}
