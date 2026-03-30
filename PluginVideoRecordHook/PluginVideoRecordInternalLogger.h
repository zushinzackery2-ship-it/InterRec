#pragma once

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace PvrcInternalLogger
{
    inline std::mutex& GetLogMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    inline bool& GetSessionHeaderState()
    {
        static bool written = false;
        return written;
    }

    inline const wchar_t* GetLogFilePath()
    {
        return L"Pvrc_internal.log";
    }

    inline void AppendUtf8Line(const char* text)
    {
        HANDLE fileHandle = CreateFileW(
            GetLogFilePath(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        DWORD written = 0;
        const DWORD length = static_cast<DWORD>(std::strlen(text));
        WriteFile(fileHandle, text, length, &written, nullptr);
        CloseHandle(fileHandle);
    }

    inline void WriteSessionHeaderIfNeeded()
    {
        if (GetSessionHeaderState())
        {
            return;
        }

        SYSTEMTIME localTime = {};
        GetLocalTime(&localTime);

        char header[256] = {};
        _snprintf_s(
            header,
            sizeof(header),
            _TRUNCATE,
            "\n[PVRC][%04u-%02u-%02u %02u:%02u:%02u][PID:%lu] ===== Session Begin =====\n",
            localTime.wYear,
            localTime.wMonth,
            localTime.wDay,
            localTime.wHour,
            localTime.wMinute,
            localTime.wSecond,
            static_cast<unsigned long>(GetCurrentProcessId()));

        HANDLE fileHandle = CreateFileW(
            GetLogFilePath(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (fileHandle != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(fileHandle, header, static_cast<DWORD>(std::strlen(header)), &written, nullptr);
            CloseHandle(fileHandle);
            GetSessionHeaderState() = true;
        }
    }

    inline void Log(const char* format, ...)
    {
        char buffer[1024] = {};

        va_list args;
        va_start(args, format);
        _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
        va_end(args);

        char finalBuffer[1152] = {};
        _snprintf_s(
            finalBuffer,
            sizeof(finalBuffer),
            _TRUNCATE,
            "[PVRC][PID:%lu] %s\n",
            static_cast<unsigned long>(GetCurrentProcessId()),
            buffer);

        std::lock_guard<std::mutex> lock(GetLogMutex());
        WriteSessionHeaderIfNeeded();
        OutputDebugStringA(finalBuffer);
        AppendUtf8Line(finalBuffer);
    }
}
