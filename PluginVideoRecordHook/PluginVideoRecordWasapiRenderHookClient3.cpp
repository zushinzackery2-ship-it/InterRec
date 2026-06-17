#include "pch.h"

#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    void RestoreAudioClient3Patches(HookRuntime& runtime)
    {
        RestoreVtablePatch(runtime.audioClient3ReleasePatch);
        RestoreVtablePatch(runtime.audioClient3GetServicePatch);
        RestoreVtablePatch(runtime.audioClient3InitializeSharedAudioStreamPatch);
    }

    bool PatchAudioClient3VtableFromProbe(IAudioClient* audioClient, std::wstring& error)
    {
        if (!audioClient)
        {
            return true;
        }

        IAudioClient3* audioClient3 = nullptr;
        HRESULT hr = audioClient->QueryInterface(IID_PPV_ARGS(&audioClient3));
        if (FAILED(hr) || !audioClient3)
        {
            return true;
        }

        void** audioClient3Vtable = nullptr;
        const bool hasVtable = TryGetVtable(audioClient3, &audioClient3Vtable);
        audioClient3->Release();
        if (!hasVtable)
        {
            error = L"读取 WASAPI IAudioClient3 vtable 失败。";
            return false;
        }

        HookRuntime& runtime = Runtime();
        if (!PatchVtableSlot(
                audioClient3Vtable,
                AudioClient3InitializeSharedAudioStreamSlot,
                reinterpret_cast<void*>(&HookedAudioClient3InitializeSharedAudioStream),
                runtime.audioClient3InitializeSharedAudioStreamPatch,
                error) ||
            !PatchVtableSlot(
                audioClient3Vtable,
                AudioClientGetServiceSlot,
                reinterpret_cast<void*>(&HookedAudioClient3GetService),
                runtime.audioClient3GetServicePatch,
                error) ||
            !PatchVtableSlot(
                audioClient3Vtable,
                IUnknownReleaseSlot,
                reinterpret_cast<void*>(&HookedAudioClient3Release),
                runtime.audioClient3ReleasePatch,
                error))
        {
            RestoreAudioClient3Patches(runtime);
            return false;
        }

        return true;
    }
}
