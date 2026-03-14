#include "pch.h"

#include "PluginVideoRecordMfInternal.h"
#include "PluginVideoRecordMfInterop.h"
#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    std::wstring BuildMfError(const wchar_t* text, HRESULT hr)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (0x%08X)", text, static_cast<unsigned int>(hr));
        return buffer;
    }

    HRESULT ConfigureVideoType(IMFMediaType* mediaType, const GUID& subtype, UINT width, UINT height)
    {
        HRESULT hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeSize(mediaType, MF_MT_FRAME_SIZE, width, height);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeRatio(mediaType, MF_MT_FRAME_RATE, DefaultFrameRate, 1);
        if (FAILED(hr))
        {
            return hr;
        }

        return MFSetAttributeRatio(mediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    }

    HRESULT WriteVideoFrameSample(
        IMFSinkWriter* sinkWriter,
        DWORD streamIndex,
        const CapturedFrame& frame,
        LONGLONG duration)
    {
        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(frame.pixels.size()), &mediaBuffer);
        if (FAILED(hr))
        {
            return hr;
        }

        BYTE* destination = nullptr;
        hr = mediaBuffer->Lock(&destination, nullptr, nullptr);
        if (FAILED(hr))
        {
            return hr;
        }

        memcpy(destination, frame.pixels.data(), frame.pixels.size());
        mediaBuffer->Unlock();
        mediaBuffer->SetCurrentLength(static_cast<DWORD>(frame.pixels.size()));

        Microsoft::WRL::ComPtr<IMFSample> sample;
        hr = MFCreateSample(&sample);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sample->AddBuffer(mediaBuffer.Get());
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sample->SetSampleTime(frame.sampleTimeHns);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sample->SetSampleDuration(duration);
        if (FAILED(hr))
        {
            return hr;
        }

        return sinkWriter->WriteSample(streamIndex, sample.Get());
    }
}
