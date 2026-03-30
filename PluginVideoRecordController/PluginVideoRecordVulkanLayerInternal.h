#pragma once

namespace PluginVideoRecord
{
    namespace PluginVideoRecordVulkanLayerInternal
    {
        std::wstring NormalizeFullPath(const std::wstring& path);
        bool FileExists(const std::wstring& path);
        bool EnsureManifestFile(
            const std::wstring& manifestPath,
            const std::wstring& hookDllPath,
            std::wstring& error);
    }
}
