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
}

namespace PluginVideoRecord
{
    PluginVideoRecordIpcServer::PluginVideoRecordIpcServer()
        : mappingHandle_(nullptr)
        , commandEvent_(nullptr)
        , stateView_(nullptr)
        , running_(false)
        , pendingSequence_(0)
        , pendingCommand_(static_cast<LONG>(CommandType::None))
        , consumedSequence_(0)
    {
    }

    PluginVideoRecordIpcServer::~PluginVideoRecordIpcServer()
    {
        Stop();
    }

    bool PluginVideoRecordIpcServer::Start()
    {
        mappingHandle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SharedState),
            MappingName);
        if (!mappingHandle_)
        {
            return false;
        }

        stateView_ = static_cast<SharedState*>(
            MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
        if (!stateView_)
        {
            CloseHandles();
            return false;
        }

        commandEvent_ = CreateEventW(nullptr, FALSE, FALSE, CommandEventName);
        if (!commandEvent_)
        {
            CloseHandles();
            return false;
        }

        ResetSharedState(*stateView_);
        InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Online));
        InterlockedExchange(&stateView_->processId, static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));

        running_.store(true);
        heartbeatThread_ = std::thread(&PluginVideoRecordIpcServer::HeartbeatThread, this);
        return true;
    }

    void PluginVideoRecordIpcServer::Stop()
    {
        running_.store(false);

        if (commandEvent_)
        {
            SetEvent(commandEvent_);
        }

        if (heartbeatThread_.joinable())
        {
            heartbeatThread_.join();
        }

        if (stateView_)
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Offline));
            InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
        }

        CloseHandles();
    }

    bool PluginVideoRecordIpcServer::TryDequeueCommand(CommandType& commandType, LONG& sequence)
    {
        sequence = pendingSequence_.load();
        if (sequence == 0 || sequence == consumedSequence_)
        {
            return false;
        }

        consumedSequence_ = sequence;
        commandType = static_cast<CommandType>(pendingCommand_.load());
        return true;
    }

    void PluginVideoRecordIpcServer::AcknowledgeCommand(CommandType commandType, LONG sequence)
    {
        if (!stateView_)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        InterlockedExchange(&stateView_->acknowledgedSequence, sequence);
        InterlockedExchange(&stateView_->lastHandledCommandType, static_cast<LONG>(commandType));
    }

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

        std::lock_guard<std::mutex> lock(stateMutex_);
        InterlockedExchange(&stateView_->recorderState, static_cast<LONG>(recorderState));
        InterlockedExchange(&stateView_->lastErrorCode, errorCode);
        CopyWideText(stateView_->currentOutputPath, _countof(stateView_->currentOutputPath), outputPath);
        CopyWideText(stateView_->lastErrorMessage, _countof(stateView_->lastErrorMessage), message);
    }

    void PluginVideoRecordIpcServer::HeartbeatThread()
    {
        while (running_.load())
        {
            DWORD waitResult = WaitForSingleObject(commandEvent_, 500);
            UpdateHeartbeat();

            if (waitResult == WAIT_OBJECT_0 && stateView_)
            {
                pendingSequence_.store(stateView_->commandSequence);
                pendingCommand_.store(stateView_->commandType);
            }
        }
    }

    void PluginVideoRecordIpcServer::UpdateHeartbeat()
    {
        if (!stateView_)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(stateMutex_);
        InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Online));
        InterlockedExchange(&stateView_->processId, static_cast<LONG>(GetCurrentProcessId()));
        InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
    }

    void PluginVideoRecordIpcServer::CloseHandles()
    {
        if (stateView_)
        {
            UnmapViewOfFile(stateView_);
            stateView_ = nullptr;
        }

        if (commandEvent_)
        {
            CloseHandle(commandEvent_);
            commandEvent_ = nullptr;
        }

        if (mappingHandle_)
        {
            CloseHandle(mappingHandle_);
            mappingHandle_ = nullptr;
        }
    }
}
