#pragma once

#ifndef PVRC_PLUGIN_VIDEO_RECORD_PROTOCOL_H
#define PVRC_PLUGIN_VIDEO_RECORD_PROTOCOL_H

#include <Windows.h>
#include <cstdint>
#include <string>

namespace PluginVideoRecord
{
    constexpr LONG ProtocolVersion = 2;
    constexpr DWORD OfflineTimeoutMs = 2000;
    constexpr DWORD SessionLeaseTimeoutMs = 2000;
    constexpr size_t MaxSessionCount = 16;
    constexpr UINT DefaultVideoBitrate = 12000000;
    constexpr UINT DefaultFrameRate = 120;
    constexpr UINT DefaultAudioSampleRate = 44100;
    constexpr UINT DefaultAudioChannels = 2;
    constexpr UINT DefaultAudioBitsPerSample = 16;
    constexpr UINT DefaultAudioBitrate = 192000;
    constexpr UINT PreviewWidth = 320;
    constexpr UINT PreviewHeight = 180;
    constexpr UINT PreviewStride = PreviewWidth * 4;
    constexpr size_t PreviewBufferBytes = static_cast<size_t>(PreviewStride) * PreviewHeight;
    constexpr size_t LogEntryCount = 256;
    constexpr size_t MaxLogLine = 256;
    constexpr size_t MaxOutputPath = 520;
    constexpr size_t MaxMessage = 256;

    constexpr wchar_t SessionRegistryMappingName[] = L"Local\\PluginVideoRecord.Registry";
    constexpr wchar_t SessionRegistryMutexName[] = L"Local\\PluginVideoRecord.RegistryWrite";
    constexpr wchar_t StateMappingPrefix[] = L"Local\\PluginVideoRecord.State.";
    constexpr wchar_t StateMutexPrefix[] = L"Local\\PluginVideoRecord.StateWrite.";
    constexpr wchar_t PreviewMappingPrefix[] = L"Local\\PluginVideoRecord.Preview.";
    constexpr wchar_t LogMappingPrefix[] = L"Local\\PluginVideoRecord.Log.";
    constexpr wchar_t CommandEventPrefix[] = L"Local\\PluginVideoRecord.Command.";

    enum class CommandType : LONG
    {
        None = 0,
        StartRecording = 1,
        StopRecording = 2,
    };

    enum class ConnectionState : LONG
    {
        Offline = 0,
        Online = 1,
    };

    enum class RecorderState : LONG
    {
        Standby = 0,
        Recording = 1,
        Error = 2,
    };

    enum class GraphicsBackend : LONG
    {
        Unknown = 0,
        Dx11 = 1,
        Dx12 = 2,
        Vulkan = 3,
    };

    enum GraphicsBackendMask : LONG
    {
        GraphicsBackendMask_None = 0,
        GraphicsBackendMask_Dx11 = 1 << 0,
        GraphicsBackendMask_Dx12 = 1 << 1,
        GraphicsBackendMask_Vulkan = 1 << 2,
    };

    inline LONG GetGraphicsBackendMask(GraphicsBackend backend)
    {
        switch (backend)
        {
        case GraphicsBackend::Dx11:
            return GraphicsBackendMask_Dx11;

        case GraphicsBackend::Dx12:
            return GraphicsBackendMask_Dx12;

        case GraphicsBackend::Vulkan:
            return GraphicsBackendMask_Vulkan;

        default:
            return GraphicsBackendMask_None;
        }
    }

    inline bool HasGraphicsBackend(LONG availableBackendMask, GraphicsBackend backend)
    {
        return (availableBackendMask & GetGraphicsBackendMask(backend)) != 0;
    }

    inline GraphicsBackend ChooseDefaultGraphicsBackend(LONG availableBackendMask)
    {
        if (HasGraphicsBackend(availableBackendMask, GraphicsBackend::Vulkan))
        {
            return GraphicsBackend::Vulkan;
        }

        if (HasGraphicsBackend(availableBackendMask, GraphicsBackend::Dx12))
        {
            return GraphicsBackend::Dx12;
        }

        if (HasGraphicsBackend(availableBackendMask, GraphicsBackend::Dx11))
        {
            return GraphicsBackend::Dx11;
        }

        return GraphicsBackend::Unknown;
    }

    struct SharedState
    {
        LONG protocolVersion;
        volatile LONG stateSequence;
        volatile LONG connectionState;
        volatile LONG recorderState;
        volatile LONG availableBackendMask;
        volatile LONG selectedBackend;
        volatile LONG activeRecordingBackend;
        volatile LONG commandSequence;
        volatile LONG commandType;
        volatile LONG acknowledgedSequence;
        volatile LONG lastHandledCommandType;
        volatile LONG lastErrorCode;
        volatile LONG processId;
        volatile LONGLONG heartbeatTickCount;
        wchar_t currentOutputPath[MaxOutputPath];
        wchar_t lastErrorMessage[MaxMessage];
    };

    struct SessionRegistrySlot
    {
        volatile LONG occupied;
        volatile LONG targetPid;
        volatile LONG controllerPid;
        volatile LONG reserved;
        volatile LONGLONG sessionId;
        volatile LONGLONG targetProcessStartTime;
        volatile LONGLONG targetHeartbeatTickCount;
        volatile LONGLONG controllerHeartbeatTickCount;
    };

    struct SessionRegistry
    {
        LONG protocolVersion;
        SessionRegistrySlot slots[MaxSessionCount];
    };

    struct PreviewFrame
    {
        LONG protocolVersion;
        volatile LONG frameSequence;
        volatile LONG isValid;
        volatile LONG width;
        volatile LONG height;
        volatile LONG stride;
        volatile LONGLONG sampleTimeHns;
        std::uint8_t pixels[PreviewBufferBytes];
    };

    struct LogEntry
    {
        volatile LONG sequence;
        wchar_t text[MaxLogLine];
    };

    struct LogBuffer
    {
        LONG protocolVersion;
        volatile LONG writeSequence;
        LogEntry entries[LogEntryCount];
    };

    inline void ResetSharedState(SharedState& state)
    {
        ZeroMemory(&state, sizeof(state));
        state.protocolVersion = ProtocolVersion;
        state.stateSequence = 0;
        state.connectionState = static_cast<LONG>(ConnectionState::Offline);
        state.recorderState = static_cast<LONG>(RecorderState::Standby);
        state.availableBackendMask = GraphicsBackendMask_None;
        state.selectedBackend = static_cast<LONG>(GraphicsBackend::Unknown);
        state.activeRecordingBackend = static_cast<LONG>(GraphicsBackend::Unknown);
    }

    inline void ResetPreviewFrame(PreviewFrame& frame)
    {
        ZeroMemory(&frame, sizeof(frame));
        frame.protocolVersion = ProtocolVersion;
        frame.stride = PreviewStride;
    }

    inline void ResetSessionRegistrySlot(SessionRegistrySlot& slot)
    {
        ZeroMemory(&slot, sizeof(slot));
    }

    inline void ResetSessionRegistry(SessionRegistry& registry)
    {
        ZeroMemory(&registry, sizeof(registry));
        registry.protocolVersion = ProtocolVersion;
    }

    inline void ResetLogBuffer(LogBuffer& logBuffer)
    {
        ZeroMemory(&logBuffer, sizeof(logBuffer));
        logBuffer.protocolVersion = ProtocolVersion;
    }

    inline std::wstring BuildSessionObjectName(const wchar_t* prefix, ULONGLONG sessionId)
    {
        wchar_t buffer[128] = {};
        swprintf_s(
            buffer,
            L"%ls%016llX",
            prefix,
            static_cast<unsigned long long>(sessionId));
        return buffer;
    }

    inline std::wstring BuildStateMappingName(ULONGLONG sessionId)
    {
        return BuildSessionObjectName(StateMappingPrefix, sessionId);
    }

    inline std::wstring BuildStateMutexName(ULONGLONG sessionId)
    {
        return BuildSessionObjectName(StateMutexPrefix, sessionId);
    }

    inline std::wstring BuildPreviewMappingName(ULONGLONG sessionId)
    {
        return BuildSessionObjectName(PreviewMappingPrefix, sessionId);
    }

    inline std::wstring BuildLogMappingName(ULONGLONG sessionId)
    {
        return BuildSessionObjectName(LogMappingPrefix, sessionId);
    }

    inline std::wstring BuildCommandEventName(ULONGLONG sessionId)
    {
        return BuildSessionObjectName(CommandEventPrefix, sessionId);
    }

    inline ULONGLONG BuildSessionId(DWORD processId, ULONGLONG processStartTime)
    {
        return processStartTime ^ (static_cast<ULONGLONG>(processId) << 32);
    }

    inline bool IsHeartbeatAlive(LONGLONG heartbeatTickCount, ULONGLONG nowTickCount, DWORD timeoutMs)
    {
        if (heartbeatTickCount == 0)
        {
            return false;
        }

        return static_cast<ULONGLONG>(heartbeatTickCount) + timeoutMs >= nowTickCount;
    }
}

#endif
