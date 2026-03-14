#pragma once

namespace PluginVideoRecord
{
    class PluginVideoRecordIpcServer
    {
    public:
        PluginVideoRecordIpcServer();
        ~PluginVideoRecordIpcServer();

        bool Start();
        void Stop();

        bool TryDequeueCommand(CommandType& commandType, LONG& sequence);
        void AcknowledgeCommand(CommandType commandType, LONG sequence);
        void SetRecorderState(
            RecorderState recorderState,
            const std::wstring& outputPath,
            const std::wstring& message,
            LONG errorCode);

    private:
        void HeartbeatThread();
        void UpdateHeartbeat();
        void CloseHandles();

        HANDLE mappingHandle_;
        HANDLE commandEvent_;
        SharedState* stateView_;
        std::thread heartbeatThread_;
        std::atomic<bool> running_;
        std::atomic<LONG> pendingSequence_;
        std::atomic<LONG> pendingCommand_;
        LONG consumedSequence_;
        std::mutex stateMutex_;
    };
}
