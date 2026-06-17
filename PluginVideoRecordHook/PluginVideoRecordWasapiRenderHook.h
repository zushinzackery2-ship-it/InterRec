#pragma once

namespace PluginVideoRecord
{
    bool InstallWasapiRenderHooks(std::wstring& error);
    void ShutdownWasapiRenderHooks();
}
