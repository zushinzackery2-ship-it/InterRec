#pragma once

namespace PluginVideoRecord
{
    class PluginVideoRecordControllerIpc
    {
    public:
        PluginVideoRecordControllerIpc();
        ~PluginVideoRecordControllerIpc();

        bool ReadState(SharedState& state);
        bool SendCommand(CommandType commandType);

    private:
        bool EnsureConnected();
        void CloseHandles();

        HANDLE mappingHandle_;
        HANDLE commandEvent_;
        SharedState* stateView_;
    };
}
