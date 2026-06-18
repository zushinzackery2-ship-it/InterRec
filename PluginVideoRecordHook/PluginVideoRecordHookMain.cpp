#include "pch.h"

#include "PluginVideoRecordInternalLogger.h"
#include "PluginVideoRecordHost.h"
#include "PluginVideoRecordWasapiRenderCapture.h"

namespace
{
    HMODULE g_moduleHandle = nullptr;

    DWORD WINAPI HostThreadEntry(LPVOID)
    {
        std::wstring audioHookError;
        if (PluginVideoRecord::PluginVideoRecordWasapiRenderCapture::InstallHooks(audioHookError))
        {
            PvrcInternalLogger::Log("[PVRC][WasapiRenderHook] early install ok");
        }
        else
        {
            PvrcInternalLogger::Log("[PVRC][WasapiRenderHook] early install failed");
        }

        PluginVideoRecord::PluginVideoRecordHost host(g_moduleHandle);
        return static_cast<DWORD>(host.Run());
    }
}

BOOL APIENTRY DllMain(HMODULE moduleHandle, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(moduleHandle);
        g_moduleHandle = moduleHandle;

        HANDLE threadHandle = CreateThread(nullptr, 0, HostThreadEntry, nullptr, 0, nullptr);
        if (threadHandle)
        {
            CloseHandle(threadHandle);
        }
    }

    return TRUE;
}
