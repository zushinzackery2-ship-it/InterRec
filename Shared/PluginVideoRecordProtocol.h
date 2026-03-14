#pragma once

#include <Windows.h>

namespace PluginVideoRecord
{
    constexpr LONG ProtocolVersion = 1;
    constexpr DWORD OfflineTimeoutMs = 2000;
    constexpr UINT DefaultVideoBitrate = 12000000;
    constexpr UINT DefaultFrameRate = 120;
    constexpr UINT DefaultAudioSampleRate = 44100;
    constexpr UINT DefaultAudioChannels = 2;
    constexpr UINT DefaultAudioBitsPerSample = 16;
    constexpr UINT DefaultAudioBitrate = 192000;
    constexpr size_t MaxOutputPath = 520;
    constexpr size_t MaxMessage = 256;

    constexpr wchar_t MappingName[] = L"Local\\PluginVideoRecord.State";
    constexpr wchar_t CommandEventName[] = L"Local\\PluginVideoRecord.Command";

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

    struct SharedState
    {
        LONG protocolVersion;
        volatile LONG connectionState;
        volatile LONG recorderState;
        volatile LONG commandSequence;
        volatile LONG commandType;
        volatile LONG acknowledgedSequence;
        volatile LONG lastHandledCommandType;
        volatile LONG lastErrorCode;
        volatile LONG processId;
        volatile LONG reserved;
        volatile LONGLONG heartbeatTickCount;
        wchar_t currentOutputPath[MaxOutputPath];
        wchar_t lastErrorMessage[MaxMessage];
    };

    inline void ResetSharedState(SharedState& state)
    {
        ZeroMemory(&state, sizeof(state));
        state.protocolVersion = ProtocolVersion;
        state.connectionState = static_cast<LONG>(ConnectionState::Offline);
        state.recorderState = static_cast<LONG>(RecorderState::Standby);
    }
}
