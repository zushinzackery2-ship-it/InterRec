#include "pch.h"

#include "../Shared/PluginVideoRecordScopedMutexLock.h"

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    bool PluginVideoRecordControllerIpc::EnsureRegistryConnected()
    {
        if (registryMappingHandle_ && registryMutexHandle_ && registryView_)
        {
            return true;
        }

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

        registryMappingHandle_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, SessionRegistryMappingName);
        if (!registryMappingHandle_)
        {
            return false;
        }

        registryView_ = static_cast<SessionRegistry*>(
            MapViewOfFile(registryMappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SessionRegistry)));
        if (!registryView_)
        {
            return false;
        }

        registryMutexHandle_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, SessionRegistryMutexName);
        if (!registryMutexHandle_)
        {
            return false;
        }

        if (registryView_->protocolVersion != ProtocolVersion)
        {
            return false;
        }

        return true;
    }

    bool PluginVideoRecordControllerIpc::RefreshSessionLease()
    {
        if (!EnsureRegistryConnected())
        {
            ReleaseSessionLease();
            CloseSessionHandles();
            return false;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            return false;
        }

        const ULONGLONG nowTickCount = GetTickCount64();
        LONG nextClaimedSlotIndex = -1;
        ULONGLONG nextClaimedSessionId = 0;

        auto releaseOwnedSlot = [this](LONG slotIndex)
        {
            if (slotIndex < 0 || slotIndex >= static_cast<LONG>(MaxSessionCount))
            {
                return;
            }

            SessionRegistrySlot& slot = registryView_->slots[slotIndex];
            if (slot.controllerPid == static_cast<LONG>(controllerProcessId_))
            {
                InterlockedExchange(&slot.controllerPid, 0);
                InterlockedExchange64(&slot.controllerHeartbeatTickCount, 0);
            }
        };

        if (claimedSlotIndex_ >= 0 && claimedSlotIndex_ < static_cast<LONG>(MaxSessionCount))
        {
            SessionRegistrySlot& slot = registryView_->slots[claimedSlotIndex_];
            const bool targetAlive = slot.occupied != FALSE &&
                slot.sessionId == static_cast<LONGLONG>(claimedSessionId_) &&
                IsHeartbeatAlive(slot.targetHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs);
            const bool controllerOwned = slot.controllerPid == static_cast<LONG>(controllerProcessId_);
            const bool controllerAlive = controllerOwned ||
                (slot.controllerPid != 0 &&
                 IsHeartbeatAlive(slot.controllerHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs));

            if (targetAlive && (controllerOwned || !controllerAlive))
            {
                InterlockedExchange(&slot.controllerPid, static_cast<LONG>(controllerProcessId_));
                InterlockedExchange64(&slot.controllerHeartbeatTickCount, static_cast<LONGLONG>(nowTickCount));
                nextClaimedSlotIndex = claimedSlotIndex_;
                nextClaimedSessionId = claimedSessionId_;
            }
            else
            {
                releaseOwnedSlot(claimedSlotIndex_);
            }
        }

        if (nextClaimedSlotIndex < 0)
        {
            for (LONG index = 0; index < static_cast<LONG>(MaxSessionCount); ++index)
            {
                SessionRegistrySlot& slot = registryView_->slots[index];
                const bool targetAlive = slot.occupied != FALSE &&
                    IsHeartbeatAlive(slot.targetHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs);
                if (!targetAlive)
                {
                    if (slot.occupied != FALSE)
                    {
                        ResetSessionRegistrySlot(slot);
                    }
                    continue;
                }

                const bool controllerOwned = slot.controllerPid == static_cast<LONG>(controllerProcessId_);
                const bool controllerAlive = controllerOwned ||
                    (slot.controllerPid != 0 &&
                     IsHeartbeatAlive(slot.controllerHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs));
                if (slot.controllerPid != 0 && controllerAlive && !controllerOwned)
                {
                    continue;
                }

                InterlockedExchange(&slot.controllerPid, static_cast<LONG>(controllerProcessId_));
                InterlockedExchange64(&slot.controllerHeartbeatTickCount, static_cast<LONGLONG>(nowTickCount));
                nextClaimedSlotIndex = index;
                nextClaimedSessionId = static_cast<ULONGLONG>(slot.sessionId);
                break;
            }
        }

        const bool sessionChanged =
            nextClaimedSlotIndex != claimedSlotIndex_ ||
            nextClaimedSessionId != claimedSessionId_;
        claimedSlotIndex_ = nextClaimedSlotIndex;
        claimedSessionId_ = nextClaimedSessionId;

        if (sessionChanged)
        {
            CloseSessionHandles();
        }

        return claimedSlotIndex_ >= 0 && claimedSessionId_ != 0;
    }

    void PluginVideoRecordControllerIpc::ReleaseSessionLease()
    {
        if (!registryView_ || !registryMutexHandle_ || claimedSlotIndex_ < 0)
        {
            claimedSlotIndex_ = -1;
            claimedSessionId_ = 0;
            return;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            return;
        }

        if (claimedSlotIndex_ < static_cast<LONG>(MaxSessionCount))
        {
            SessionRegistrySlot& slot = registryView_->slots[claimedSlotIndex_];
            if (slot.controllerPid == static_cast<LONG>(controllerProcessId_) &&
                slot.sessionId == static_cast<LONGLONG>(claimedSessionId_))
            {
                InterlockedExchange(&slot.controllerPid, 0);
                InterlockedExchange64(&slot.controllerHeartbeatTickCount, 0);
            }
        }

        claimedSlotIndex_ = -1;
        claimedSessionId_ = 0;
    }

}
