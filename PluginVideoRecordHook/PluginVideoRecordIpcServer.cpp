#include "pch.h"

#include "../Shared/PluginVideoRecordScopedMutexLock.h"

#include "PluginVideoRecordIpcServer.h"

namespace PluginVideoRecord
{
    PluginVideoRecordIpcServer::PluginVideoRecordIpcServer()
        : registryMappingHandle_(nullptr)
        , registryMutexHandle_(nullptr)
        , mappingHandle_(nullptr)
        , logMappingHandle_(nullptr)
        , commandEvent_(nullptr)
        , stateMutexHandle_(nullptr)
        , registryView_(nullptr)
        , stateView_(nullptr)
        , logView_(nullptr)
        , running_(false)
        , pendingSequence_(0)
        , pendingCommand_(static_cast<LONG>(CommandType::None))
        , processId_(0)
        , processStartTime_(0)
        , sessionId_(0)
        , sessionSlotIndex_(-1)
        , consumedSequence_(0)
        , observedSequence_(0)
    {
    }

    PluginVideoRecordIpcServer::~PluginVideoRecordIpcServer()
    {
        Stop();
    }

    bool PluginVideoRecordIpcServer::Start()
    {
        if (!StartRegistry())
        {
            return false;
        }

        if (!ClaimSessionSlot())
        {
            StopRegistry();
            return false;
        }

        stateMappingName_ = BuildStateMappingName(sessionId_);
        logMappingName_ = BuildLogMappingName(sessionId_);
        previewMappingName_ = BuildPreviewMappingName(sessionId_);
        commandEventName_ = BuildCommandEventName(sessionId_);
        stateMutexName_ = BuildStateMutexName(sessionId_);

        mappingHandle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SharedState),
            stateMappingName_.c_str());
        if (!mappingHandle_)
        {
            Stop();
            return false;
        }

        stateView_ = static_cast<SharedState*>(
            MapViewOfFile(mappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
        if (!stateView_)
        {
            Stop();
            return false;
        }

        logMappingHandle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(LogBuffer),
            logMappingName_.c_str());
        if (!logMappingHandle_)
        {
            Stop();
            return false;
        }

        logView_ = static_cast<LogBuffer*>(
            MapViewOfFile(logMappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LogBuffer)));
        if (!logView_)
        {
            Stop();
            return false;
        }

        commandEvent_ = CreateEventW(nullptr, FALSE, FALSE, commandEventName_.c_str());
        if (!commandEvent_)
        {
            Stop();
            return false;
        }

        stateMutexHandle_ = CreateMutexW(nullptr, FALSE, stateMutexName_.c_str());
        if (!stateMutexHandle_)
        {
            Stop();
            return false;
        }

        ResetSharedState(*stateView_);
        ResetLogBuffer(*logView_);
        WriteStateLocked(
            [this]()
            {
                InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Online));
                InterlockedExchange(&stateView_->processId, static_cast<LONG>(processId_));
                InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
            });

        running_.store(true);
        heartbeatThread_ = std::thread(&PluginVideoRecordIpcServer::HeartbeatThread, this);
        UpdateSessionHeartbeat();
        AppendLog(L"IPC 服务已启动。");
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
            WriteStateLocked(
                [this]()
                {
                    InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Offline));
                    InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
                });
        }

        ReleaseSessionSlot();
        CloseHandles();
        StopRegistry();
    }

    ULONGLONG PluginVideoRecordIpcServer::GetSessionId() const
    {
        return sessionId_;
    }

    bool PluginVideoRecordIpcServer::TryPeekCommand(CommandType& commandType, LONG& sequence) const
    {
        sequence = 0;
        if (!stateView_)
        {
            return false;
        }

        const LONG liveSequence = stateView_->commandSequence;
        if (liveSequence == 0 || liveSequence == consumedSequence_)
        {
            return false;
        }

        sequence = liveSequence;
        commandType = static_cast<CommandType>(stateView_->commandType);
        return true;
    }

    void PluginVideoRecordIpcServer::AcknowledgeCommand(CommandType commandType, LONG sequence)
    {
        if (!stateView_)
        {
            return;
        }

        consumedSequence_ = sequence;
        observedSequence_ = sequence;

        WriteStateLocked(
            [this, commandType, sequence]()
            {
                InterlockedExchange(&stateView_->acknowledgedSequence, sequence);
                InterlockedExchange(&stateView_->lastHandledCommandType, static_cast<LONG>(commandType));
            });
    }

    void PluginVideoRecordIpcServer::HeartbeatThread()
    {
        while (running_.load())
        {
            if (commandEvent_)
            {
                WaitForSingleObject(commandEvent_, 500);
            }
            else
            {
                Sleep(500);
            }

            UpdateHeartbeat();
            UpdateSessionHeartbeat();

            if (stateView_)
            {
                const LONG liveSequence = stateView_->commandSequence;
                if (liveSequence != 0 && liveSequence != observedSequence_)
                {
                    observedSequence_ = liveSequence;
                    const CommandType liveCommandType = static_cast<CommandType>(stateView_->commandType);
                    wchar_t buffer[96] = {};
                    swprintf_s(
                        buffer,
                        L"IPC 观察到命令：type=%ld seq=%ld ack=%ld。",
                        static_cast<LONG>(liveCommandType),
                        liveSequence,
                        stateView_->acknowledgedSequence);
                    AppendLog(buffer);
                }
            }
        }
    }

    void PluginVideoRecordIpcServer::UpdateHeartbeat()
    {
        if (!stateView_)
        {
            return;
        }

        WriteStateLocked(
            [this]()
            {
                InterlockedExchange(&stateView_->connectionState, static_cast<LONG>(ConnectionState::Online));
                InterlockedExchange(&stateView_->processId, static_cast<LONG>(processId_));
                InterlockedExchange64(&stateView_->heartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
            });
    }

}
