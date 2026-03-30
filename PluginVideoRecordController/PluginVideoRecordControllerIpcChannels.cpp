#include "pch.h"

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    bool PluginVideoRecordControllerIpc::EnsureStateConnected()
    {
        if (!RefreshSessionLease())
        {
            return false;
        }

        if (stateMappingHandle_ && commandEvent_ && stateView_ && stateMutexHandle_)
        {
            return true;
        }

        CloseStateHandles();

        const std::wstring stateMappingName = BuildStateMappingName(claimedSessionId_);
        const std::wstring commandEventName = BuildCommandEventName(claimedSessionId_);
        const std::wstring stateMutexName = BuildStateMutexName(claimedSessionId_);

        stateMappingHandle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, stateMappingName.c_str());
        if (!stateMappingHandle_)
        {
            return false;
        }

        stateView_ = static_cast<SharedState*>(
            MapViewOfFile(stateMappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
        if (!stateView_)
        {
            CloseStateHandles();
            return false;
        }

        commandEvent_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, commandEventName.c_str());
        if (!commandEvent_)
        {
            CloseStateHandles();
            return false;
        }

        stateMutexHandle_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, stateMutexName.c_str());
        if (!stateMutexHandle_)
        {
            CloseStateHandles();
            return false;
        }

        return true;
    }

    bool PluginVideoRecordControllerIpc::EnsurePreviewConnected()
    {
        if (!RefreshSessionLease())
        {
            return false;
        }

        if (previewMappingHandle_ && previewView_)
        {
            return true;
        }

        ClosePreviewHandles();

        const std::wstring previewMappingName = BuildPreviewMappingName(claimedSessionId_);
        previewMappingHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, previewMappingName.c_str());
        if (!previewMappingHandle_)
        {
            return false;
        }

        previewView_ = static_cast<PreviewFrame*>(
            MapViewOfFile(previewMappingHandle_, FILE_MAP_READ, 0, 0, sizeof(PreviewFrame)));
        if (!previewView_)
        {
            ClosePreviewHandles();
            return false;
        }

        return true;
    }

    bool PluginVideoRecordControllerIpc::EnsureLogConnected()
    {
        if (!RefreshSessionLease())
        {
            return false;
        }

        if (logMappingHandle_ && logView_)
        {
            return true;
        }

        CloseLogHandles();

        const std::wstring logMappingName = BuildLogMappingName(claimedSessionId_);
        logMappingHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, logMappingName.c_str());
        if (!logMappingHandle_)
        {
            return false;
        }

        logView_ = static_cast<LogBuffer*>(
            MapViewOfFile(logMappingHandle_, FILE_MAP_READ, 0, 0, sizeof(LogBuffer)));
        if (!logView_)
        {
            CloseLogHandles();
            return false;
        }

        if (logView_->protocolVersion != ProtocolVersion)
        {
            CloseLogHandles();
            return false;
        }

        lastLogSequence_ = 0;
        return true;
    }

    void PluginVideoRecordControllerIpc::CloseSessionHandles()
    {
        CloseLogHandles();
        ClosePreviewHandles();
        CloseStateHandles();
    }

    void PluginVideoRecordControllerIpc::CloseStateHandles()
    {
        if (stateView_)
        {
            UnmapViewOfFile(stateView_);
            stateView_ = nullptr;
        }

        if (stateMutexHandle_)
        {
            CloseHandle(stateMutexHandle_);
            stateMutexHandle_ = nullptr;
        }

        if (commandEvent_)
        {
            CloseHandle(commandEvent_);
            commandEvent_ = nullptr;
        }

        if (stateMappingHandle_)
        {
            CloseHandle(stateMappingHandle_);
            stateMappingHandle_ = nullptr;
        }
    }

    void PluginVideoRecordControllerIpc::ClosePreviewHandles()
    {
        if (previewView_)
        {
            UnmapViewOfFile(previewView_);
            previewView_ = nullptr;
        }

        if (previewMappingHandle_)
        {
            CloseHandle(previewMappingHandle_);
            previewMappingHandle_ = nullptr;
        }
    }

    void PluginVideoRecordControllerIpc::CloseLogHandles()
    {
        if (logView_)
        {
            UnmapViewOfFile(logView_);
            logView_ = nullptr;
        }

        if (logMappingHandle_)
        {
            CloseHandle(logMappingHandle_);
            logMappingHandle_ = nullptr;
        }

        lastLogSequence_ = 0;
    }
}
