#pragma once

namespace PluginVideoRecord
{
    struct PreviewFrameSnapshot
    {
        bool isValid;
        UINT width;
        UINT height;
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
        bool SendCommand(CommandType commandType);

    private:
        bool EnsureStateConnected();
        bool EnsurePreviewConnected();
        bool EnsureLogConnected();
        void CloseStateHandles();
        void ClosePreviewHandles();
        void CloseLogHandles();

        HANDLE stateMappingHandle_;
        HANDLE previewMappingHandle_;
        HANDLE logMappingHandle_;
        HANDLE commandEvent_;
        SharedState* stateView_;
        PreviewFrame* previewView_;
        LogBuffer* logView_;
        LONG lastLogSequence_;
    };
}
