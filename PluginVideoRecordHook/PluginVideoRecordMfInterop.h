#pragma once

namespace PluginVideoRecord
{
    struct CapturedFrame;
    struct CapturedAudioPacket;

    struct SinkWriterStreams
    {
        DWORD videoStreamIndex;
        DWORD audioStreamIndex;
    };

    std::wstring BuildMfError(const wchar_t* text, HRESULT hr);

    HRESULT CreateSinkWriter(
        const std::wstring& outputPath,
        UINT width,
        UINT height,
        Microsoft::WRL::ComPtr<IMFSinkWriter>& sinkWriter,
        SinkWriterStreams& streamIndices);

    HRESULT WriteVideoFrameSample(
        IMFSinkWriter* sinkWriter,
        DWORD streamIndex,
        const CapturedFrame& frame,
        LONGLONG duration);

    HRESULT WriteAudioSample(
        IMFSinkWriter* sinkWriter,
        DWORD streamIndex,
        const CapturedAudioPacket& packet);
}
