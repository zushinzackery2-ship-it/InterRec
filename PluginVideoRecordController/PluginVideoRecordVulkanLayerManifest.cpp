#include "pch.h"

#include "PluginVideoRecordVulkanLayer.h"
#include "PluginVideoRecordVulkanLayerInternal.h"

namespace
{
    std::wstring GetDirectoryPath(const std::wstring& path)
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return std::wstring();
        }

        return path.substr(0, slash);
    }

    bool EnsureDirectoryExists(const std::wstring& directoryPath)
    {
        if (directoryPath.empty())
        {
            return false;
        }

        const std::wstring normalizedPath = PluginVideoRecord::PluginVideoRecordVulkanLayerInternal::NormalizeFullPath(directoryPath);
        size_t start = 0;
        if (normalizedPath.size() >= 3 && normalizedPath[1] == L':')
        {
            start = 3;
        }

        while (start < normalizedPath.size())
        {
            const size_t separator = normalizedPath.find_first_of(L"\\/", start);
            const std::wstring partialPath = separator == std::wstring::npos
                ? normalizedPath
                : normalizedPath.substr(0, separator);
            if (!partialPath.empty())
            {
                if (!CreateDirectoryW(partialPath.c_str(), nullptr))
                {
                    const DWORD lastError = GetLastError();
                    if (lastError != ERROR_ALREADY_EXISTS)
                    {
                        return false;
                    }
                }
            }

            if (separator == std::wstring::npos)
            {
                break;
            }

            start = separator + 1;
        }

        return true;
    }

    bool ReadUtf8TextFile(const std::wstring& path, std::string& text)
    {
        text.clear();

        HANDLE fileHandle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        LARGE_INTEGER fileSize = {};
        if (!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart < 0)
        {
            CloseHandle(fileHandle);
            return false;
        }

        text.resize(static_cast<size_t>(fileSize.QuadPart));
        DWORD bytesRead = 0;
        const BOOL readOk = text.empty() ||
            ReadFile(fileHandle, text.data(), static_cast<DWORD>(text.size()), &bytesRead, nullptr);
        CloseHandle(fileHandle);
        return readOk == TRUE && bytesRead == text.size();
    }

    bool WriteUtf8TextFile(const std::wstring& path, const std::string& text)
    {
        HANDLE fileHandle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD bytesWritten = 0;
        const BOOL writeOk = text.empty() ||
            WriteFile(fileHandle, text.data(), static_cast<DWORD>(text.size()), &bytesWritten, nullptr);
        CloseHandle(fileHandle);
        return writeOk == TRUE && bytesWritten == text.size();
    }

    std::string ToUtf8(const std::wstring& text)
    {
        if (text.empty())
        {
            return std::string();
        }

        const int utf8Length = WideCharToMultiByte(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (utf8Length <= 0)
        {
            return std::string();
        }

        std::string utf8(utf8Length, '\0');
        WideCharToMultiByte(
            CP_UTF8,
            0,
            text.c_str(),
            static_cast<int>(text.size()),
            utf8.data(),
            utf8Length,
            nullptr,
            nullptr);
        return utf8;
    }

    std::string EscapeJsonString(const std::wstring& text)
    {
        const std::string utf8 = ToUtf8(text);
        std::string escaped;
        escaped.reserve(utf8.size() * 2);

        for (const unsigned char ch : utf8)
        {
            if (ch == '\\' || ch == '"')
            {
                escaped.push_back('\\');
            }

            escaped.push_back(static_cast<char>(ch));
        }

        return escaped;
    }

    std::string BuildManifestText(const std::wstring& hookDllPath)
    {
        const std::string escapedLibraryPath = EscapeJsonString(hookDllPath);

        std::string text;
        text.reserve(512 + escapedLibraryPath.size());
        text += "{\n";
        text += "  \"file_format_version\": \"1.1.2\",\n";
        text += "  \"layer\": {\n";
        text += "    \"name\": \"VK_LAYER_PVRC_capture\",\n";
        text += "    \"type\": \"GLOBAL\",\n";
        text += "    \"library_path\": \"";
        text += escapedLibraryPath;
        text += "\",\n";
        text += "    \"api_version\": \"1.3.0\",\n";
        text += "    \"implementation_version\": \"1\",\n";
        text += "    \"description\": \"PVRC Vulkan capture layer\",\n";
        text += "    \"functions\": {\n";
        text += "      \"vkNegotiateLoaderLayerInterfaceVersion\": \"vkNegotiateLoaderLayerInterfaceVersion\",\n";
        text += "      \"vkGetInstanceProcAddr\": \"vkGetInstanceProcAddr\",\n";
        text += "      \"vkGetDeviceProcAddr\": \"vkGetDeviceProcAddr\"\n";
        text += "    },\n";
        text += "    \"disable_environment\": {\n";
        text += "      \"PVRC_DISABLE_VULKAN_LAYER\": \"1\"\n";
        text += "    }\n";
        text += "  }\n";
        text += "}\n";
        return text;
    }
}

namespace PluginVideoRecord
{
    namespace PluginVideoRecordVulkanLayerInternal
    {
        std::wstring NormalizeFullPath(const std::wstring& path)
        {
            DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
            if (length == 0)
            {
                return path;
            }

            std::wstring normalized(length, L'\0');
            length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(normalized.size()), normalized.data(), nullptr);
            if (length == 0 || length >= normalized.size())
            {
                return path;
            }

            normalized.resize(length);
            return normalized;
        }

        bool FileExists(const std::wstring& path)
        {
            const DWORD attributes = GetFileAttributesW(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        bool EnsureManifestFile(
            const std::wstring& manifestPath,
            const std::wstring& hookDllPath,
            std::wstring& error)
        {
            error.clear();

            const std::wstring normalizedManifestPath = NormalizeFullPath(manifestPath);
            const std::wstring normalizedHookDllPath = NormalizeFullPath(hookDllPath);
            if (!FileExists(normalizedHookDllPath))
            {
                error = L"缺少 PluginVideoRecordVkLayer.dll。";
                return false;
            }

            const std::wstring manifestDirectory = GetDirectoryPath(normalizedManifestPath);
            if (!EnsureDirectoryExists(manifestDirectory))
            {
                error = L"创建 Vulkan Layer 运行时目录失败。";
                return false;
            }

            const std::string desiredText = BuildManifestText(normalizedHookDllPath);
            std::string existingText;
            if (ReadUtf8TextFile(normalizedManifestPath, existingText) && existingText == desiredText)
            {
                return true;
            }

            if (!WriteUtf8TextFile(normalizedManifestPath, desiredText))
            {
                error = L"写入 Vulkan Layer 运行时清单失败。";
                return false;
            }

            return true;
        }
    }

}
