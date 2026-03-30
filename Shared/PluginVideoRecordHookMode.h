#pragma once

#ifndef PVRC_PLUGIN_VIDEO_RECORD_HOOK_MODE_H
#define PVRC_PLUGIN_VIDEO_RECORD_HOOK_MODE_H

#include <Windows.h>

#include <string>

namespace PluginVideoRecord
{
    namespace HookMode
    {
        constexpr wchar_t HookModeKey[] = L"Software\\InterRec\\HookMode";
        constexpr wchar_t ForceAutoHookValueName[] = L"ForceAutoHook";

        inline bool IsForceAutoHookEnabled()
        {
            DWORD value = 0;
            DWORD valueSize = sizeof(value);
            const LONG result = RegGetValueW(
                HKEY_CURRENT_USER,
                HookModeKey,
                ForceAutoHookValueName,
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &valueSize);
            return result == ERROR_SUCCESS && value != 0;
        }

        inline bool SetForceAutoHookEnabled(bool enabled, std::wstring& error)
        {
            error.clear();

            HKEY keyHandle = nullptr;
            DWORD disposition = 0;
            const LONG createResult = RegCreateKeyExW(
                HKEY_CURRENT_USER,
                HookModeKey,
                0,
                nullptr,
                0,
                KEY_QUERY_VALUE | KEY_SET_VALUE,
                nullptr,
                &keyHandle,
                &disposition);
            if (createResult != ERROR_SUCCESS)
            {
                error = L"无法打开强制非Layer模式注册表项。";
                return false;
            }

            LONG result = ERROR_SUCCESS;
            if (enabled)
            {
                const DWORD value = 1;
                result = RegSetValueExW(
                    keyHandle,
                    ForceAutoHookValueName,
                    0,
                    REG_DWORD,
                    reinterpret_cast<const BYTE*>(&value),
                    sizeof(value));
            }
            else
            {
                result = RegDeleteValueW(keyHandle, ForceAutoHookValueName);
                if (result == ERROR_FILE_NOT_FOUND)
                {
                    result = ERROR_SUCCESS;
                }
            }

            RegCloseKey(keyHandle);
            if (result != ERROR_SUCCESS)
            {
                error = enabled ? L"启用强制非Layer模式失败。" : L"关闭强制非Layer模式失败。";
                return false;
            }

            return true;
        }
    }
}

#endif
