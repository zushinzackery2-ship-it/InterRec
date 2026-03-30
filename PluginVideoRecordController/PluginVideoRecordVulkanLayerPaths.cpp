#include "pch.h"

#include "PluginVideoRecordVulkanLayer.h"
#include "PluginVideoRecordVulkanLayerInternal.h"

namespace
{
    constexpr wchar_t VulkanLayerManifestFileName[] = L"PluginVideoRecordVkLayer.json";
    constexpr wchar_t VulkanLayerHookFileName[] = L"PluginVideoRecordVkLayer.dll";
    constexpr wchar_t RuntimeManifestDirectorySuffix[] = L"\\InterRec\\VulkanLayer";

    std::wstring GetDirectoryPath(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return std::wstring();
        }

        return path.substr(0, slash);
    }

    std::wstring CombinePath(const std::wstring& directory, const wchar_t* fileName)
    {
        if (directory.empty())
        {
            return std::wstring(fileName);
        }

        std::wstring path = directory;
        if (path.back() != L'\\' && path.back() != L'/')
        {
            path.push_back(L'\\');
        }

        path += fileName;
        return path;
    }

    std::wstring ExpandEnvironmentString(const wchar_t* value)
    {
        DWORD length = ExpandEnvironmentStringsW(value, nullptr, 0);
        if (length == 0)
        {
            return std::wstring();
        }

        std::wstring expanded(length, L'\0');
        length = ExpandEnvironmentStringsW(value, expanded.data(), static_cast<DWORD>(expanded.size()));
        if (length == 0 || length > expanded.size())
        {
            return std::wstring();
        }

        expanded.resize(length - 1);
        return expanded;
    }

    std::wstring GetExecutableDirectory()
    {
        wchar_t modulePath[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameW(nullptr, modulePath, _countof(modulePath));
        if (length == 0 || length >= _countof(modulePath))
        {
            return std::wstring();
        }

        return GetDirectoryPath(std::wstring(modulePath, length));
    }

    std::wstring GetRuntimeManifestDirectory()
    {
        std::wstring runtimeDirectory = ExpandEnvironmentString(L"%LOCALAPPDATA%");
        if (!runtimeDirectory.empty())
        {
            runtimeDirectory += RuntimeManifestDirectorySuffix;
            return runtimeDirectory;
        }

        return GetExecutableDirectory();
    }
}

namespace PluginVideoRecord
{
    namespace PluginVideoRecordVulkanLayer
    {
        std::wstring GetHookDllPath()
        {
            return PluginVideoRecordVulkanLayerInternal::NormalizeFullPath(
                CombinePath(GetExecutableDirectory(), VulkanLayerHookFileName));
        }

        std::wstring GetManifestPath()
        {
            return PluginVideoRecordVulkanLayerInternal::NormalizeFullPath(
                CombinePath(GetRuntimeManifestDirectory(), VulkanLayerManifestFileName));
        }

        bool IsAvailable()
        {
            return PluginVideoRecordVulkanLayerInternal::FileExists(GetHookDllPath());
        }

        bool EnsureManifestReady(std::wstring& error)
        {
            if (!IsAvailable())
            {
                error = L"缺少 PluginVideoRecordVkLayer.dll。";
                return false;
            }

            return PluginVideoRecordVulkanLayerInternal::EnsureManifestFile(
                GetManifestPath(),
                GetHookDllPath(),
                error);
        }
    }
}
