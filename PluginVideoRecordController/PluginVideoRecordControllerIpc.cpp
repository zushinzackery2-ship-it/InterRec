#include "pch.h"

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    PluginVideoRecordControllerIpc::PluginVideoRecordControllerIpc()
        : mappingHandle_(nullptr)
        , commandEvent_(nullptr)
        , stateView_(nullptr)
    {
    }

    PluginVideoRecordControllerIpc::~PluginVideoRecordControllerIpc()
    {
        CloseHandles();
    }

    bool PluginVideoRecordControllerIpc::ReadState(SharedState& state)
    {
        if (!EnsureConnected())
        {
            ResetSharedState(state);
            return false;
        }

        CopyMemory(&state, stateView_, sizeof(SharedState));
        return true;
    }

    bool PluginVideoRecordControllerIpc::SendCommand(CommandType commandType)
    {
        if (!EnsureConnected())
        {
            return false;
        }

        LONG nextSequence = stateView_->commandSequence + 1;
        InterlockedExchange(&stateView_->commandType, static_cast<LONG>(commandType));
        InterlockedExchange(&stateView_->commandSequence, nextSequence);
        return SetEvent(commandEvent_) == TRUE;
    }

    bool PluginVideoRecordControllerIpc::EnsureConnected()
    {
        if (mappingHandle_ && commandEvent_ && stateView_)
        {
            return true;
        }

        CloseHandles();

        mappingHandle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName);
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

        commandEvent_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommandEventName);
        if (!commandEvent_)
        {
            CloseHandles();
            return false;
        }

        return true;
    }

    void PluginVideoRecordControllerIpc::CloseHandles()
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
