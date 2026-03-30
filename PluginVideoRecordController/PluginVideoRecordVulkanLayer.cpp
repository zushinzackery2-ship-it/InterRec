#include "pch.h"

#include "PluginVideoRecordVulkanLayer.h"
#include "PluginVideoRecordVulkanLayerInternal.h"

namespace
{
    constexpr wchar_t VulkanImplicitLayersKey[] = L"Software\\Khronos\\Vulkan\\ImplicitLayers";
    constexpr wchar_t VulkanLayerManifestFileName[] = L"PluginVideoRecordVkLayer.json";

    struct RegistryLocation
    {
        HKEY rootKey;
        REGSAM samDesired;
        const wchar_t* name;
    };

    const RegistryLocation RegistryLocations[] =
    {
        { HKEY_CURRENT_USER, KEY_WOW64_64KEY, L"HKCU(64)" },
        { HKEY_CURRENT_USER, KEY_WOW64_32KEY, L"HKCU(32)" },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"HKLM(64)" },
        { HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"HKLM(32)" }
    };

    std::vector<std::wstring> EnumerateRegisteredPluginVideoRecordLayerPaths(const RegistryLocation& location)
    {
        std::vector<std::wstring> paths;

        HKEY keyHandle = nullptr;
        const LONG openResult = RegOpenKeyExW(
            location.rootKey,
            VulkanImplicitLayersKey,
            0,
            KEY_QUERY_VALUE | location.samDesired,
            &keyHandle);
        if (openResult != ERROR_SUCCESS)
        {
            return paths;
        }

        DWORD valueCount = 0;
        DWORD maxValueNameLength = 0;
        const LONG infoResult = RegQueryInfoKeyW(
            keyHandle,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &valueCount,
            &maxValueNameLength,
            nullptr,
            nullptr,
            nullptr);
        if (infoResult == ERROR_SUCCESS)
        {
            std::vector<wchar_t> valueName(maxValueNameLength + 2, L'\0');
            for (DWORD index = 0; index < valueCount; ++index)
            {
                DWORD valueNameLength = maxValueNameLength + 1;
                const LONG enumResult = RegEnumValueW(
                    keyHandle,
                    index,
                    valueName.data(),
                    &valueNameLength,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr);
                if (enumResult != ERROR_SUCCESS)
                {
                    continue;
                }

                std::wstring path(valueName.data(), valueNameLength);
                const size_t slash = path.find_last_of(L"\\/");
                const wchar_t* fileName = slash == std::wstring::npos ? path.c_str() : path.c_str() + slash + 1;
                if (_wcsicmp(fileName, VulkanLayerManifestFileName) == 0)
                {
                    paths.push_back(path);
                }
            }
        }

        RegCloseKey(keyHandle);
        return paths;
    }
}

namespace PluginVideoRecord
{
    namespace PluginVideoRecordVulkanLayer
    {
        bool IsEnabled()
        {
            const std::wstring normalizedManifestPath =
                PluginVideoRecordVulkanLayerInternal::NormalizeFullPath(GetManifestPath());

            for (const RegistryLocation& location : RegistryLocations)
            {
                HKEY keyHandle = nullptr;
                const LONG openResult = RegOpenKeyExW(
                    location.rootKey,
                    VulkanImplicitLayersKey,
                    0,
                    KEY_QUERY_VALUE | location.samDesired,
                    &keyHandle);
                if (openResult != ERROR_SUCCESS)
                {
                    continue;
                }

                DWORD value = 1;
                DWORD valueSize = sizeof(value);
                const LONG queryResult = RegQueryValueExW(
                    keyHandle,
                    normalizedManifestPath.c_str(),
                    nullptr,
                    nullptr,
                    reinterpret_cast<BYTE*>(&value),
                    &valueSize);
                RegCloseKey(keyHandle);
                if (queryResult == ERROR_SUCCESS && value == 0)
                {
                    return true;
                }
            }

            return false;
        }

        bool SetEnabled(bool enabled, std::wstring& error)
        {
            error.clear();

            const std::wstring normalizedManifestPath =
                PluginVideoRecordVulkanLayerInternal::NormalizeFullPath(GetManifestPath());
            if (enabled)
            {
                if (!EnsureManifestReady(error))
                {
                    return false;
                }
            }

            bool wroteAny = false;
            bool hklmAccessDenied = false;

            for (const RegistryLocation& location : RegistryLocations)
            {
                HKEY keyHandle = nullptr;
                DWORD disposition = 0;
                LONG result = RegCreateKeyExW(
                    location.rootKey,
                    VulkanImplicitLayersKey,
                    0,
                    nullptr,
                    REG_OPTION_NON_VOLATILE,
                    KEY_QUERY_VALUE | KEY_SET_VALUE | location.samDesired,
                    nullptr,
                    &keyHandle,
                    &disposition);
                if (result != ERROR_SUCCESS)
                {
                    if (location.rootKey == HKEY_LOCAL_MACHINE && result == ERROR_ACCESS_DENIED)
                    {
                        hklmAccessDenied = true;
                        continue;
                    }

                    if (!wroteAny)
                    {
                        error = std::wstring(L"无法打开 Vulkan layer 注册表项：") + location.name;
                        return false;
                    }

                    continue;
                }

                bool cleanupOk = true;
                const std::vector<std::wstring> stalePaths =
                    EnumerateRegisteredPluginVideoRecordLayerPaths(location);
                for (const std::wstring& stalePath : stalePaths)
                {
                    const LONG deleteResult = RegDeleteValueW(keyHandle, stalePath.c_str());
                    if (deleteResult != ERROR_SUCCESS && deleteResult != ERROR_FILE_NOT_FOUND)
                    {
                        cleanupOk = false;
                    }
                }

                if (cleanupOk)
                {
                    if (enabled)
                    {
                        const DWORD value = 0;
                        result = RegSetValueExW(
                            keyHandle,
                            normalizedManifestPath.c_str(),
                            0,
                            REG_DWORD,
                            reinterpret_cast<const BYTE*>(&value),
                            sizeof(value));
                    }
                    else
                    {
                        result = RegDeleteValueW(keyHandle, normalizedManifestPath.c_str());
                        if (result == ERROR_FILE_NOT_FOUND)
                        {
                            result = ERROR_SUCCESS;
                        }
                    }
                }
                else
                {
                    result = ERROR_WRITE_FAULT;
                }

                RegCloseKey(keyHandle);
                if (result == ERROR_SUCCESS)
                {
                    wroteAny = true;
                }
                else if (!wroteAny)
                {
                    error = enabled ? L"写入 Vulkan layer 注册表失败。" : L"删除 Vulkan layer 注册表失败。";
                    return false;
                }
            }

            if (!wroteAny)
            {
                error = hklmAccessDenied
                    ? L"没有权限写入 HKLM/HKLM Wow6432Node，管理员进程可能无法加载 Vulkan-Layer模式。"
                    : L"没有任何 Vulkan layer 注册表项被成功修改。";
                return false;
            }

            if (hklmAccessDenied)
            {
                error = L"已写入当前用户注册表；如果目标进程是管理员权限，仍可能不会加载 Vulkan-Layer模式。";
            }

            return true;
        }
    }
}
