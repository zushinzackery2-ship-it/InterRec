#pragma once

#include <Windows.h>
#include <cstdint>

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
    constexpr UINT PreviewWidth = 320;
    constexpr UINT PreviewHeight = 180;
    constexpr UINT PreviewStride = PreviewWidth * 4;
    constexpr size_t PreviewBufferBytes = static_cast<size_t>(PreviewStride) * PreviewHeight;
    constexpr size_t LogEntryCount = 256;
    constexpr size_t MaxLogLine = 256;
    constexpr size_t MaxOutputPath = 520;
    constexpr size_t MaxMessage = 256;

    constexpr wchar_t MappingName[] = L"Local\\PluginVideoRecord.State";
    constexpr wchar_t PreviewMappingName[] = L"Local\\PluginVideoRecord.Preview";
    constexpr wchar_t LogMappingName[] = L"Local\\PluginVideoRecord.Log";
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

    enum class GraphicsBackend : LONG
    {
        Unknown = 0,
        Dx11 = 1,
        Dx12 = 2,
    };

    struct SharedState
    {
        LONG protocolVersion;
        volatile LONG connectionState;
        volatile LONG recorderState;
        volatile LONG graphicsBackend;
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
        state.connectionState = static_cast<LONG>(ConnectionState::Offline);
        state.recorderState = static_cast<LONG>(RecorderState::Standby);
        state.graphicsBackend = static_cast<LONG>(GraphicsBackend::Unknown);
    }

    inline void ResetPreviewFrame(PreviewFrame& frame)
    {
        ZeroMemory(&frame, sizeof(frame));
        frame.protocolVersion = ProtocolVersion;
        frame.stride = PreviewStride;
    }

    inline void ResetLogBuffer(LogBuffer& logBuffer)
    {
        ZeroMemory(&logBuffer, sizeof(logBuffer));
        logBuffer.protocolVersion = ProtocolVersion;
    }
}
