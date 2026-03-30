#include "pch.h"

#include "PluginVideoRecordIpcServer.h"

namespace
{
    void CopyWideText(wchar_t* destination, size_t destinationCount, const std::wstring& value)
    {
        if (!destination || destinationCount == 0)
        {
            return;
        }

        wcsncpy_s(destination, destinationCount, value.c_str(), _TRUNCATE);
    }

    std::wstring BuildTimestampedLogLine(const std::wstring& message)
    {
        SYSTEMTIME localTime = {};
        GetLocalTime(&localTime);
        const DWORD processId = GetCurrentProcessId();

        wchar_t buffer[PluginVideoRecord::MaxLogLine] = {};
        swprintf_s(
            buffer,
            L"[%02u:%02u:%02u][PID:%lu] %ls",
            localTime.wHour,
            localTime.wMinute,
            localTime.wSecond,
            static_cast<unsigned long>(processId),
            message.c_str());
        return buffer;
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordIpcServer::SetRecorderState(
        RecorderState recorderState,
        const std::wstring& outputPath,
        const std::wstring& message,
        LONG errorCode)
    {
        if (!stateView_)
        {
            return;
        }

        WriteStateLocked(
            [this, recorderState, &outputPath, &message, errorCode]()
            {
                InterlockedExchange(&stateView_->recorderState, static_cast<LONG>(recorderState));
                InterlockedExchange(&stateView_->lastErrorCode, errorCode);
                CopyWideText(stateView_->currentOutputPath, _countof(stateView_->currentOutputPath), outputPath);
                CopyWideText(stateView_->lastErrorMessage, _countof(stateView_->lastErrorMessage), message);
            });
    }

    void PluginVideoRecordIpcServer::SetAvailableBackendMask(LONG availableBackendMask)
    {
        if (!stateView_)
        {
            return;
        }

        WriteStateLocked(
            [this, availableBackendMask]()
            {
                InterlockedExchange(&stateView_->availableBackendMask, availableBackendMask);
            });
    }

    void PluginVideoRecordIpcServer::SetSelectedBackend(GraphicsBackend graphicsBackend)
    {
        if (!stateView_)
        {
            return;
        }

        WriteStateLocked(
            [this, graphicsBackend]()
            {
                InterlockedExchange(&stateView_->selectedBackend, static_cast<LONG>(graphicsBackend));
            });
    }

    GraphicsBackend PluginVideoRecordIpcServer::GetSelectedBackend() const
    {
        if (!stateView_)
        {
            return GraphicsBackend::Unknown;
        }

        return static_cast<GraphicsBackend>(stateView_->selectedBackend);
    }

    void PluginVideoRecordIpcServer::SetActiveRecordingBackend(GraphicsBackend graphicsBackend)
    {
        if (!stateView_)
        {
            return;
        }

        WriteStateLocked(
            [this, graphicsBackend]()
            {
                InterlockedExchange(&stateView_->activeRecordingBackend, static_cast<LONG>(graphicsBackend));
            });
    }

    void PluginVideoRecordIpcServer::AppendLog(const std::wstring& message)
    {
        if (!logView_ || message.empty())
        {
            return;
        }

        std::lock_guard<std::mutex> lock(logMutex_);
        const LONG nextSequence = logView_->writeSequence + 1;
        LogEntry& entry = logView_->entries[(nextSequence - 1) % LogEntryCount];
        entry.sequence = 0;
        CopyWideText(entry.text, _countof(entry.text), BuildTimestampedLogLine(message));
        InterlockedExchange(&entry.sequence, nextSequence);
        InterlockedExchange(&logView_->writeSequence, nextSequence);
    }
}
