#include "pch.h"

#include "../Shared/PluginVideoRecordScopedMutexLock.h"

#include "PluginVideoRecordIpcServer.h"

namespace
{
    ULONGLONG GetCurrentProcessStartTime()
    {
        FILETIME creationTime = {};
        FILETIME exitTime = {};
        FILETIME kernelTime = {};
        FILETIME userTime = {};
        if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
        {
            return 0;
        }

        ULARGE_INTEGER value = {};
        value.LowPart = creationTime.dwLowDateTime;
        value.HighPart = creationTime.dwHighDateTime;
        return value.QuadPart;
    }
}

namespace PluginVideoRecord
{
    bool PluginVideoRecordIpcServer::StartRegistry()
    {
        processId_ = GetCurrentProcessId();
        processStartTime_ = GetCurrentProcessStartTime();
        sessionId_ = 0;
        sessionSlotIndex_ = -1;

        registryMappingHandle_ = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            sizeof(SessionRegistry),
            SessionRegistryMappingName);
        if (!registryMappingHandle_)
        {
            return false;
        }

        const DWORD lastError = GetLastError();
        registryView_ = static_cast<SessionRegistry*>(
            MapViewOfFile(registryMappingHandle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SessionRegistry)));
        if (!registryView_)
        {
            StopRegistry();
            return false;
        }

        registryMutexHandle_ = CreateMutexW(nullptr, FALSE, SessionRegistryMutexName);
        if (!registryMutexHandle_)
        {
            StopRegistry();
            return false;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            StopRegistry();
            return false;
        }

        if (lastError != ERROR_ALREADY_EXISTS || registryView_->protocolVersion != ProtocolVersion)
        {
            ResetSessionRegistry(*registryView_);
        }

        return true;
    }

    void PluginVideoRecordIpcServer::StopRegistry()
    {
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

        processId_ = 0;
        processStartTime_ = 0;
        sessionId_ = 0;
        sessionSlotIndex_ = -1;
    }

    bool PluginVideoRecordIpcServer::ClaimSessionSlot()
    {
        if (!registryView_ || !registryMutexHandle_)
        {
            return false;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            return false;
        }

        const ULONGLONG nowTickCount = GetTickCount64();
        LONG claimedSlotIndex = -1;

        for (LONG index = 0; index < static_cast<LONG>(MaxSessionCount); ++index)
        {
            SessionRegistrySlot& slot = registryView_->slots[index];
            const bool targetAlive = slot.occupied != FALSE &&
                IsHeartbeatAlive(slot.targetHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs);
            if (slot.occupied != FALSE &&
                slot.targetPid == static_cast<LONG>(processId_) &&
                slot.targetProcessStartTime == static_cast<LONGLONG>(processStartTime_))
            {
                claimedSlotIndex = index;
                break;
            }

            if (slot.occupied == FALSE || !targetAlive)
            {
                ResetSessionRegistrySlot(slot);
                claimedSlotIndex = index;
                break;
            }
        }

        if (claimedSlotIndex < 0)
        {
            return false;
        }

        SessionRegistrySlot& slot = registryView_->slots[claimedSlotIndex];
        sessionId_ = slot.sessionId != 0
            ? static_cast<ULONGLONG>(slot.sessionId)
            : BuildSessionId(processId_, processStartTime_);
        sessionSlotIndex_ = claimedSlotIndex;

        InterlockedExchange(&slot.occupied, TRUE);
        InterlockedExchange(&slot.targetPid, static_cast<LONG>(processId_));
        InterlockedExchange64(&slot.targetProcessStartTime, static_cast<LONGLONG>(processStartTime_));
        InterlockedExchange64(&slot.sessionId, static_cast<LONGLONG>(sessionId_));
        InterlockedExchange64(&slot.targetHeartbeatTickCount, static_cast<LONGLONG>(nowTickCount));

        if (!IsHeartbeatAlive(slot.controllerHeartbeatTickCount, nowTickCount, SessionLeaseTimeoutMs))
        {
            InterlockedExchange(&slot.controllerPid, 0);
            InterlockedExchange64(&slot.controllerHeartbeatTickCount, 0);
        }

        return true;
    }

    void PluginVideoRecordIpcServer::ReleaseSessionSlot()
    {
        if (!registryView_ || !registryMutexHandle_ || sessionSlotIndex_ < 0)
        {
            return;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            return;
        }

        if (sessionSlotIndex_ >= static_cast<LONG>(MaxSessionCount))
        {
            sessionSlotIndex_ = -1;
            sessionId_ = 0;
            return;
        }

        SessionRegistrySlot& slot = registryView_->slots[sessionSlotIndex_];
        if (slot.occupied != FALSE &&
            slot.targetPid == static_cast<LONG>(processId_) &&
            slot.targetProcessStartTime == static_cast<LONGLONG>(processStartTime_) &&
            slot.sessionId == static_cast<LONGLONG>(sessionId_))
        {
            ResetSessionRegistrySlot(slot);
        }

        sessionSlotIndex_ = -1;
        sessionId_ = 0;
    }

    void PluginVideoRecordIpcServer::UpdateSessionHeartbeat()
    {
        if (!registryView_ || !registryMutexHandle_ || sessionSlotIndex_ < 0)
        {
            return;
        }

        ScopedMutexLock registryLock(registryMutexHandle_);
        if (!registryLock.IsLocked())
        {
            return;
        }

        if (sessionSlotIndex_ >= static_cast<LONG>(MaxSessionCount))
        {
            return;
        }

        SessionRegistrySlot& slot = registryView_->slots[sessionSlotIndex_];
        if (slot.occupied == FALSE ||
            slot.targetPid != static_cast<LONG>(processId_) ||
            slot.targetProcessStartTime != static_cast<LONGLONG>(processStartTime_) ||
            slot.sessionId != static_cast<LONGLONG>(sessionId_))
        {
            return;
        }

        InterlockedExchange64(&slot.targetHeartbeatTickCount, static_cast<LONGLONG>(GetTickCount64()));
    }

    void PluginVideoRecordIpcServer::CloseHandles()
    {
        if (logView_)
        {
            UnmapViewOfFile(logView_);
            logView_ = nullptr;
        }

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

        if (logMappingHandle_)
        {
            CloseHandle(logMappingHandle_);
            logMappingHandle_ = nullptr;
        }

        if (mappingHandle_)
        {
            CloseHandle(mappingHandle_);
            mappingHandle_ = nullptr;
        }

        stateMappingName_.clear();
        logMappingName_.clear();
        previewMappingName_.clear();
        commandEventName_.clear();
        stateMutexName_.clear();
    }
}
