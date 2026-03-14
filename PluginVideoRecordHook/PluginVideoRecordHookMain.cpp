#include "pch.h"

#include "PluginVideoRecordHost.h"

namespace
{
    HMODULE g_moduleHandle = nullptr;

    DWORD WINAPI HostThreadEntry(LPVOID)
    {
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
