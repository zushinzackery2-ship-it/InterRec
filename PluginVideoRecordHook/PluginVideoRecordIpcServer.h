#pragma once

#include "../Shared/PluginVideoRecordScopedMutexLock.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordIpcServer
    {
    public:
        PluginVideoRecordIpcServer();
        ~PluginVideoRecordIpcServer();

        bool Start();
        void Stop();
        ULONGLONG GetSessionId() const;

        bool TryPeekCommand(CommandType& commandType, LONG& sequence) const;
        void AcknowledgeCommand(CommandType commandType, LONG sequence);
        void SetRecorderState(
            RecorderState recorderState,
            const std::wstring& outputPath,
            const std::wstring& message,
            LONG errorCode);
        void SetAvailableBackendMask(LONG availableBackendMask);
        void SetSelectedBackend(GraphicsBackend graphicsBackend);
        GraphicsBackend GetSelectedBackend() const;
        void SetActiveRecordingBackend(GraphicsBackend graphicsBackend);
        void AppendLog(const std::wstring& message);

    private:
        void HeartbeatThread();
        void UpdateHeartbeat();
        bool StartRegistry();
        void StopRegistry();
        bool ClaimSessionSlot();
        void ReleaseSessionSlot();
        void UpdateSessionHeartbeat();
        void CloseHandles();
        template <typename WriterFn>
        void WriteStateLocked(WriterFn writer)
        {
            if (!stateView_)
            {
                return;
            }

            ScopedMutexLock stateWriteLock(stateMutexHandle_);
            if (!stateWriteLock.IsLocked())
            {
                return;
            }

            std::lock_guard<std::mutex> lock(stateMutex_);
            InterlockedIncrement(&stateView_->stateSequence);
            MemoryBarrier();
            writer();
            MemoryBarrier();
            InterlockedIncrement(&stateView_->stateSequence);
        }

        HANDLE registryMappingHandle_;
        HANDLE registryMutexHandle_;
        HANDLE mappingHandle_;
        HANDLE logMappingHandle_;
        HANDLE commandEvent_;
        HANDLE stateMutexHandle_;
        SessionRegistry* registryView_;
        SharedState* stateView_;
        LogBuffer* logView_;
        std::thread heartbeatThread_;
        std::atomic<bool> running_;
        std::atomic<LONG> pendingSequence_;
        std::atomic<LONG> pendingCommand_;
        DWORD processId_;
        ULONGLONG processStartTime_;
        ULONGLONG sessionId_;
        LONG sessionSlotIndex_;
        std::wstring stateMappingName_;
        std::wstring logMappingName_;
        std::wstring previewMappingName_;
        std::wstring commandEventName_;
        std::wstring stateMutexName_;
        LONG consumedSequence_;
        LONG observedSequence_;
        std::mutex stateMutex_;
        std::mutex logMutex_;
    };
}
