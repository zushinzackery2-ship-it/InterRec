#pragma once

namespace PluginVideoRecord
{
    namespace PluginVideoRecordVulkanLayer
    {
        std::wstring GetHookDllPath();
        std::wstring GetManifestPath();
        bool IsAvailable();
        bool EnsureManifestReady(std::wstring& error);
        bool IsEnabled();
        bool SetEnabled(bool enabled, std::wstring& error);
    }
}
