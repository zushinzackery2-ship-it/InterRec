#include "pch.h"

#include "PluginVideoRecordMfInternal.h"
#include "PluginVideoRecordMfInterop.h"

namespace PluginVideoRecord
{
    HRESULT CreateSinkWriter(
        const std::wstring& outputPath,
        UINT width,
        UINT height,
        Microsoft::WRL::ComPtr<IMFSinkWriter>& sinkWriter,
        SinkWriterStreams& streamIndices)
    {
        streamIndices.videoStreamIndex = static_cast<DWORD>(-1);
        streamIndices.audioStreamIndex = static_cast<DWORD>(-1);

        Microsoft::WRL::ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 2);
        if (FAILED(hr))
        {
            return hr;
        }

        attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);

        hr = MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, attributes.Get(), &sinkWriter);
        if (FAILED(hr))
        {
            return hr;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> videoOutputType;
        hr = MFCreateMediaType(&videoOutputType);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = ConfigureVideoType(videoOutputType.Get(), MFVideoFormat_H264, width, height);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = videoOutputType->SetUINT32(MF_MT_AVG_BITRATE, DefaultVideoBitrate);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sinkWriter->AddStream(videoOutputType.Get(), &streamIndices.videoStreamIndex);
        if (FAILED(hr))
        {
            return hr;
        }

        const GUID inputFormats[] = { MFVideoFormat_RGB32, MFVideoFormat_ARGB32 };
        for (const GUID& inputFormat : inputFormats)
        {
            Microsoft::WRL::ComPtr<IMFMediaType> videoInputType;
            hr = MFCreateMediaType(&videoInputType);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = ConfigureVideoType(videoInputType.Get(), inputFormat, width, height);
            if (FAILED(hr))
            {
                return hr;
            }

            hr = videoInputType->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width * 4u));
            if (FAILED(hr))
            {
                return hr;
            }

            hr = sinkWriter->SetInputMediaType(streamIndices.videoStreamIndex, videoInputType.Get(), nullptr);
            if (SUCCEEDED(hr))
            {
                break;
            }
        }

        if (FAILED(hr))
        {
            return hr;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> audioOutputType;
        hr = MFCreateMediaType(&audioOutputType);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = ConfigureAacAudioType(audioOutputType.Get());
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sinkWriter->AddStream(audioOutputType.Get(), &streamIndices.audioStreamIndex);
        if (FAILED(hr))
        {
            return hr;
        }

        Microsoft::WRL::ComPtr<IMFMediaType> audioInputType;
        hr = MFCreateMediaType(&audioInputType);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = ConfigurePcmAudioType(audioInputType.Get());
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sinkWriter->SetInputMediaType(streamIndices.audioStreamIndex, audioInputType.Get(), nullptr);
        if (FAILED(hr))
        {
            return hr;
        }

        return sinkWriter->BeginWriting();
    }
}
