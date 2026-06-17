#include "pch.h"

#include "PluginVideoRecordInternalLogger.h"
#include "PluginVideoRecordWasapiRenderHook.h"
#include "PluginVideoRecordWasapiRenderHookInternal.h"

namespace
{
    using namespace PluginVideoRecord::WasapiRenderHookInternal;

    bool CreateProbeAudioClient(
        Microsoft::WRL::ComPtr<IAudioClient>& audioClient,
        std::wstring& error)
    {
        Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator));
        if (FAILED(hr))
        {
            error = BuildHresultText(L"创建 WASAPI 设备枚举器失败。", hr);
            return false;
        }

        Microsoft::WRL::ComPtr<IMMDevice> device;
        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr))
        {
            error = BuildHresultText(L"获取默认 WASAPI 输出设备失败。", hr);
            return false;
        }

        hr = device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(audioClient.GetAddressOf()));
        if (FAILED(hr))
        {
            error = BuildHresultText(L"激活 WASAPI IAudioClient 失败。", hr);
            return false;
        }

        return true;
    }

    bool InitializeProbeRenderClient(
        IAudioClient* audioClient,
        Microsoft::WRL::ComPtr<IAudioRenderClient>& renderClient,
        std::wstring& error)
    {
        WAVEFORMATEX* mixFormat = nullptr;
        HRESULT hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr) || !mixFormat)
        {
            error = BuildHresultText(L"读取 WASAPI mix format 失败。", hr);
            return false;
        }

        hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, mixFormat, nullptr);
        CoTaskMemFree(mixFormat);
        if (FAILED(hr))
        {
            error = BuildHresultText(L"初始化 WASAPI probe render client 失败。", hr);
            return false;
        }

        hr = audioClient->GetService(IID_PPV_ARGS(&renderClient));
        if (FAILED(hr) || !renderClient)
        {
            error = BuildHresultText(L"读取 WASAPI IAudioRenderClient 失败。", hr);
            return false;
        }

        return true;
    }

    void RestoreAudioClientPatches(HookRuntime& runtime)
    {
        RestoreAudioClient3Patches(runtime);
        RestoreVtablePatch(runtime.audioClientReleasePatch);
        RestoreVtablePatch(runtime.audioClientGetServicePatch);
        RestoreVtablePatch(runtime.audioClientInitializePatch);
    }

    void RestoreRenderClientPatches(HookRuntime& runtime)
    {
        RestoreVtablePatch(runtime.renderClientReleasePatch);
        RestoreVtablePatch(runtime.renderClientReleaseBufferPatch);
        RestoreVtablePatch(runtime.renderClientGetBufferPatch);
    }

    bool PatchAudioClientVtable(void** vtable, HookRuntime& runtime, std::wstring& error)
    {
        if (!PatchVtableSlot(
                vtable,
                AudioClientInitializeSlot,
                reinterpret_cast<void*>(&HookedAudioClientInitialize),
                runtime.audioClientInitializePatch,
                error) ||
            !PatchVtableSlot(
                vtable,
                AudioClientGetServiceSlot,
                reinterpret_cast<void*>(&HookedAudioClientGetService),
                runtime.audioClientGetServicePatch,
                error) ||
            !PatchVtableSlot(
                vtable,
                IUnknownReleaseSlot,
                reinterpret_cast<void*>(&HookedAudioClientRelease),
                runtime.audioClientReleasePatch,
                error))
        {
            RestoreAudioClientPatches(runtime);
            return false;
        }

        return true;
    }

    bool PatchRenderClientVtable(void** vtable, HookRuntime& runtime, std::wstring& error)
    {
        if (!PatchVtableSlot(
                vtable,
                AudioRenderClientGetBufferSlot,
                reinterpret_cast<void*>(&HookedRenderClientGetBuffer),
                runtime.renderClientGetBufferPatch,
                error) ||
            !PatchVtableSlot(
                vtable,
                AudioRenderClientReleaseBufferSlot,
                reinterpret_cast<void*>(&HookedRenderClientReleaseBuffer),
                runtime.renderClientReleaseBufferPatch,
                error) ||
            !PatchVtableSlot(
                vtable,
                IUnknownReleaseSlot,
                reinterpret_cast<void*>(&HookedRenderClientRelease),
                runtime.renderClientReleasePatch,
                error))
        {
            RestoreRenderClientPatches(runtime);
            return false;
        }

        return true;
    }
}

namespace PluginVideoRecord
{
    bool InstallWasapiRenderHooks(std::wstring& error)
    {
        HookRuntime& runtime = Runtime();
        {
            std::lock_guard<std::mutex> lock(runtime.mutex);
            if (runtime.installed)
            {
                return true;
            }
        }

        PvrcInternalLogger::Log("[PVRC][WasapiRenderHook] install begin");

        HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool didInitializeCom = SUCCEEDED(coHr);
        if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE)
        {
            error = BuildHresultText(L"初始化 WASAPI hook COM 环境失败。", coHr);
            return false;
        }

        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient;
        if (!CreateProbeAudioClient(audioClient, error))
        {
            if (didInitializeCom)
            {
                CoUninitialize();
            }

            return false;
        }

        void** audioClientVtable = nullptr;
        if (!TryGetVtable(audioClient.Get(), &audioClientVtable))
        {
            error = L"读取 WASAPI IAudioClient vtable 失败。";
            audioClient.Reset();
            if (didInitializeCom)
            {
                CoUninitialize();
            }

            return false;
        }

        {
            std::lock_guard<std::mutex> lock(runtime.mutex);
            if (!PatchAudioClientVtable(audioClientVtable, runtime, error) ||
                !PatchAudioClient3VtableFromProbe(audioClient.Get(), error))
            {
                RestoreAudioClientPatches(runtime);
                audioClient.Reset();
                if (didInitializeCom)
                {
                    CoUninitialize();
                }

                return false;
            }
        }

        if (!InitializeProbeRenderClient(audioClient.Get(), renderClient, error))
        {
            ShutdownWasapiRenderHooks();
            audioClient.Reset();
            if (didInitializeCom)
            {
                CoUninitialize();
            }

            return false;
        }

        void** renderClientVtable = nullptr;
        if (!TryGetVtable(renderClient.Get(), &renderClientVtable))
        {
            error = L"读取 WASAPI IAudioRenderClient vtable 失败。";
            ShutdownWasapiRenderHooks();
            renderClient.Reset();
            audioClient.Reset();
            if (didInitializeCom)
            {
                CoUninitialize();
            }

            return false;
        }

        bool renderClientPatched = false;
        {
            std::lock_guard<std::mutex> lock(runtime.mutex);
            renderClientPatched = PatchRenderClientVtable(renderClientVtable, runtime, error);
            if (renderClientPatched)
            {
                runtime.installed = true;
            }
        }

        if (!renderClientPatched)
        {
            ShutdownWasapiRenderHooks();
            renderClient.Reset();
            audioClient.Reset();
            if (didInitializeCom)
            {
                CoUninitialize();
            }

            return false;
        }

        renderClient.Reset();
        audioClient.Reset();
        if (didInitializeCom)
        {
            CoUninitialize();
        }

        PvrcInternalLogger::Log("[PVRC][WasapiRenderHook] install end");
        return true;
    }

    void ShutdownWasapiRenderHooks()
    {
        HookRuntime& runtime = Runtime();
        std::lock_guard<std::mutex> lock(runtime.mutex);
        RestoreVtablePatch(runtime.renderClientReleasePatch);
        RestoreVtablePatch(runtime.renderClientReleaseBufferPatch);
        RestoreVtablePatch(runtime.renderClientGetBufferPatch);
        RestoreAudioClientPatches(runtime);
        runtime.renderClients.clear();
        runtime.audioClients.clear();
        runtime.installed = false;
        PvrcInternalLogger::Log("[PVRC][WasapiRenderHook] shutdown");
    }
}
