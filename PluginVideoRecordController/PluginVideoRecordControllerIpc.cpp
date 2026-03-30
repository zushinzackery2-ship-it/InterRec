#include "pch.h"

#include "../Shared/PluginVideoRecordScopedMutexLock.h"

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    PluginVideoRecordControllerIpc::PluginVideoRecordControllerIpc()
        : registryMappingHandle_(nullptr)
        , registryMutexHandle_(nullptr)
        , stateMappingHandle_(nullptr)
        , previewMappingHandle_(nullptr)
        , logMappingHandle_(nullptr)
        , commandEvent_(nullptr)
        , stateMutexHandle_(nullptr)
        , registryView_(nullptr)
        , stateView_(nullptr)
        , previewView_(nullptr)
        , logView_(nullptr)
        , controllerProcessId_(GetCurrentProcessId())
        , claimedSlotIndex_(-1)
        , claimedSessionId_(0)
        , lastLogSequence_(0)
    {
    }

    PluginVideoRecordControllerIpc::~PluginVideoRecordControllerIpc()
    {
        ReleaseSessionLease();
        CloseLogHandles();
        ClosePreviewHandles();
        CloseStateHandles();

        if (registryView_)
        {
            UnmapViewOfFile(registryView_);
            registryView_ = nullptr;
        }

        if (registryMutexHandle_)
        {
            CloseHandle(registryMutexHandle_);
            registryMutexHandle_ = nullptr;
        }

        if (registryMappingHandle_)
        {
            CloseHandle(registryMappingHandle_);
            registryMappingHandle_ = nullptr;
        }
    }

    bool PluginVideoRecordControllerIpc::ReadState(SharedState& state)
    {
        if (!EnsureStateConnected())
        {
            ResetSharedState(state);
            return false;
        }

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const LONG beginSequence = stateView_->stateSequence;
            if ((beginSequence & 1) != 0)
            {
                Sleep(0);
                SwitchToThread();
                continue;
            }

            MemoryBarrier();
            CopyMemory(&state, stateView_, sizeof(SharedState));
            MemoryBarrier();

            const LONG endSequence = stateView_->stateSequence;
            if (beginSequence == endSequence && (endSequence & 1) == 0)
            {
                if (state.protocolVersion == ProtocolVersion)
                {
                    return true;
                }

                CloseStateHandles();
                break;
            }

            Sleep(0);
            SwitchToThread();
        }

        ResetSharedState(state);
        return false;
    }

    bool PluginVideoRecordControllerIpc::ReadPreview(PreviewFrameSnapshot& preview)
    {
        if (!EnsurePreviewConnected())
        {
            return false;
        }

        PreviewFrameSnapshot nextPreview = {};
        nextPreview.isValid = false;
        nextPreview.width = 0;
        nextPreview.height = 0;
        nextPreview.sampleTimeHns = 0;
        nextPreview.pixels.clear();

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const LONG beginSequence = previewView_->frameSequence;
            if ((beginSequence & 1) != 0)
            {
                Sleep(0);
                SwitchToThread();
                continue;
            }

            MemoryBarrier();

            if (previewView_->protocolVersion != ProtocolVersion)
            {
                ClosePreviewHandles();
                return false;
            }

            const LONG isValid = previewView_->isValid;
            const LONG width = previewView_->width;
            const LONG height = previewView_->height;
            const LONG stride = previewView_->stride;
            const LONGLONG sampleTimeHns = previewView_->sampleTimeHns;
            const bool hasPixels = isValid != FALSE && width > 0 && height > 0 && stride >= 4;
            const size_t pixelBytes = hasPixels ? static_cast<size_t>(height) * stride : 0;
            if (pixelBytes > PreviewBufferBytes)
            {
                ClosePreviewHandles();
                return false;
            }

            if (hasPixels)
            {
                nextPreview.pixels.resize(pixelBytes);
                CopyMemory(nextPreview.pixels.data(), previewView_->pixels, pixelBytes);
            }

            MemoryBarrier();
            const LONG endSequence = previewView_->frameSequence;
            if (beginSequence == endSequence && (endSequence & 1) == 0)
            {
                nextPreview.isValid = hasPixels;
                nextPreview.width = hasPixels ? static_cast<UINT>(width) : 0;
                nextPreview.height = hasPixels ? static_cast<UINT>(height) : 0;
                nextPreview.sampleTimeHns = hasPixels ? sampleTimeHns : 0;
                if (!hasPixels)
                {
                    nextPreview.pixels.clear();
                }

                preview = std::move(nextPreview);
                return true;
            }

            Sleep(0);
            SwitchToThread();
        }

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

    bool PluginVideoRecordControllerIpc::SetSelectedBackend(GraphicsBackend graphicsBackend)
    {
        if (!EnsureStateConnected())
        {
            return false;
        }

        ScopedMutexLock stateWriteLock(stateMutexHandle_);
        if (!stateWriteLock.IsLocked())
        {
            return false;
        }

        InterlockedIncrement(&stateView_->stateSequence);
        MemoryBarrier();
        InterlockedExchange(&stateView_->selectedBackend, static_cast<LONG>(graphicsBackend));
        MemoryBarrier();
        InterlockedIncrement(&stateView_->stateSequence);
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

}
