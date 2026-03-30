#pragma once

namespace PluginVideoRecord
{
    struct PreviewFrameSnapshot
    {
        bool isValid;
        UINT width;
        UINT height;
        LONGLONG sampleTimeHns;
        std::vector<std::uint8_t> pixels;
    };

    class PluginVideoRecordControllerIpc
    {
    public:
        PluginVideoRecordControllerIpc();
        ~PluginVideoRecordControllerIpc();

        bool ReadState(SharedState& state);
        bool ReadPreview(PreviewFrameSnapshot& preview);
        bool ReadLogs(std::vector<std::wstring>& logLines);
        bool SetSelectedBackend(GraphicsBackend graphicsBackend);
        bool SendCommand(CommandType commandType);

    private:
        bool EnsureRegistryConnected();
        bool RefreshSessionLease();
        void ReleaseSessionLease();
        bool EnsureStateConnected();
        bool EnsurePreviewConnected();
        bool EnsureLogConnected();
        void CloseSessionHandles();
        void CloseStateHandles();
        void ClosePreviewHandles();
        void CloseLogHandles();

        HANDLE registryMappingHandle_;
        HANDLE registryMutexHandle_;
        HANDLE stateMappingHandle_;
        HANDLE previewMappingHandle_;
        HANDLE logMappingHandle_;
        HANDLE commandEvent_;
        HANDLE stateMutexHandle_;
        SessionRegistry* registryView_;
        SharedState* stateView_;
        PreviewFrame* previewView_;
        LogBuffer* logView_;
        DWORD controllerProcessId_;
        LONG claimedSlotIndex_;
        ULONGLONG claimedSessionId_;
        LONG lastLogSequence_;
    };
}
