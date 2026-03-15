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

        bool TryPeekCommand(CommandType& commandType, LONG& sequence) const;
        void AcknowledgeCommand(CommandType commandType, LONG sequence);
        void SetRecorderState(
            RecorderState recorderState,
            const std::wstring& outputPath,
            const std::wstring& message,
            LONG errorCode);
        void SetGraphicsBackend(GraphicsBackend graphicsBackend);
        void AppendLog(const std::wstring& message);

    private:
        void HeartbeatThread();
        void UpdateHeartbeat();
        void CloseHandles();

        HANDLE mappingHandle_;
        HANDLE logMappingHandle_;
        HANDLE commandEvent_;
        SharedState* stateView_;
        LogBuffer* logView_;
        std::thread heartbeatThread_;
        std::atomic<bool> running_;
        std::atomic<LONG> pendingSequence_;
        std::atomic<LONG> pendingCommand_;
        LONG consumedSequence_;
        std::mutex stateMutex_;
        std::mutex logMutex_;
    };
}
