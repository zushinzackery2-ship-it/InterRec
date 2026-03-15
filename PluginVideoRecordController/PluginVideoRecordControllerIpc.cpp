#include "pch.h"

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    PluginVideoRecordControllerIpc::PluginVideoRecordControllerIpc()
        : stateMappingHandle_(nullptr)
        , previewMappingHandle_(nullptr)
        , logMappingHandle_(nullptr)
        , commandEvent_(nullptr)
        , stateView_(nullptr)
        , previewView_(nullptr)
        , logView_(nullptr)
        , lastLogSequence_(0)
    {
    }

    PluginVideoRecordControllerIpc::~PluginVideoRecordControllerIpc()
    {
        CloseLogHandles();
        ClosePreviewHandles();
        CloseStateHandles();
    }

    bool PluginVideoRecordControllerIpc::ReadState(SharedState& state)
    {
        if (!EnsureStateConnected())
        {
            ResetSharedState(state);
            return false;
        }

        CopyMemory(&state, stateView_, sizeof(SharedState));
        return true;
    }

    bool PluginVideoRecordControllerIpc::ReadPreview(PreviewFrameSnapshot& preview)
    {
        preview.isValid = false;
        preview.width = 0;
        preview.height = 0;
        preview.pixels.clear();

        if (!EnsurePreviewConnected())
        {
            return false;
        }

        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const LONG beginSequence = previewView_->frameSequence;
            if ((beginSequence & 1) != 0)
            {
                continue;
            }

            if (previewView_->protocolVersion != ProtocolVersion)
            {
                ClosePreviewHandles();
                return false;
            }

            const LONG isValid = previewView_->isValid;
            const LONG width = previewView_->width;
            const LONG height = previewView_->height;
            const LONG stride = previewView_->stride;
            const bool hasPixels = isValid != FALSE && width > 0 && height > 0 && stride >= 4;
            const size_t pixelBytes = hasPixels ? static_cast<size_t>(height) * stride : 0;
            if (pixelBytes > PreviewBufferBytes)
            {
                ClosePreviewHandles();
                return false;
            }

            if (hasPixels)
            {
                preview.pixels.resize(pixelBytes);
                CopyMemory(preview.pixels.data(), previewView_->pixels, pixelBytes);
            }

            const LONG endSequence = previewView_->frameSequence;
            if (beginSequence == endSequence && (endSequence & 1) == 0)
            {
                preview.isValid = hasPixels;
                preview.width = hasPixels ? static_cast<UINT>(width) : 0;
                preview.height = hasPixels ? static_cast<UINT>(height) : 0;
                if (!hasPixels)
                {
                    preview.pixels.clear();
                }

                return true;
            }
        }

        preview.pixels.clear();
        return false;
    }

    bool PluginVideoRecordControllerIpc::ReadLogs(std::vector<std::wstring>& logLines)
    {
        logLines.clear();
        if (!EnsureLogConnected())
        {
            return false;
        }

        const LONG writeSequence = logView_->writeSequence;
        if (writeSequence <= lastLogSequence_)
        {
            return true;
        }

        LONG firstSequence = lastLogSequence_ + 1;
        const LONG minimumAvailable = writeSequence - static_cast<LONG>(LogEntryCount) + 1;
        if (firstSequence < minimumAvailable)
        {
            firstSequence = minimumAvailable;
        }

        for (LONG sequence = firstSequence; sequence <= writeSequence; ++sequence)
        {
            const LogEntry& entry = logView_->entries[(sequence - 1) % LogEntryCount];
            if (entry.sequence != sequence)
            {
                continue;
            }

            logLines.emplace_back(entry.text);
        }

        lastLogSequence_ = writeSequence;
        return true;
    }

    bool PluginVideoRecordControllerIpc::SendCommand(CommandType commandType)
    {
        if (!EnsureStateConnected())
        {
            return false;
        }

        LONG nextSequence = stateView_->commandSequence + 1;
        InterlockedExchange(&stateView_->commandType, static_cast<LONG>(commandType));
        InterlockedExchange(&stateView_->commandSequence, nextSequence);
        return SetEvent(commandEvent_) == TRUE;
    }

    bool PluginVideoRecordControllerIpc::EnsureStateConnected()
    {
        if (stateMappingHandle_ && commandEvent_ && stateView_)
        {
            return true;
        }

        CloseStateHandles();

        stateMappingHandle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName);
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

        commandEvent_ = OpenEventW(EVENT_MODIFY_STATE, FALSE, CommandEventName);
        if (!commandEvent_)
        {
            CloseStateHandles();
            return false;
        }

        return true;
    }

    bool PluginVideoRecordControllerIpc::EnsurePreviewConnected()
    {
        if (previewMappingHandle_ && previewView_)
        {
            return true;
        }

        ClosePreviewHandles();

        previewMappingHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, PreviewMappingName);
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
        if (logMappingHandle_ && logView_)
        {
            return true;
        }

        CloseLogHandles();

        logMappingHandle_ = OpenFileMappingW(FILE_MAP_READ, FALSE, LogMappingName);
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

    void PluginVideoRecordControllerIpc::CloseStateHandles()
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
