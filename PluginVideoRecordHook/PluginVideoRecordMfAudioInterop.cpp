#include "pch.h"

#include "PluginVideoRecordMfInternal.h"
#include "PluginVideoRecordMfInterop.h"
#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    HRESULT ConfigurePcmAudioType(IMFMediaType* mediaType)
    {
        const UINT32 blockAlign = DefaultAudioChannels * DefaultAudioBitsPerSample / 8;
        const UINT32 avgBytesPerSecond = DefaultAudioSampleRate * blockAlign;

        HRESULT hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, DefaultAudioChannels);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, DefaultAudioSampleRate);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, DefaultAudioBitsPerSample);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avgBytesPerSecond);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        if (FAILED(hr))
        {
            return hr;
        }

        return mediaType->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
    }

    HRESULT ConfigureAacAudioType(IMFMediaType* mediaType)
    {
        HRESULT hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, DefaultAudioChannels);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, DefaultAudioSampleRate);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, DefaultAudioBitsPerSample);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, DefaultAudioBitrate / 8);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = mediaType->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);
        if (FAILED(hr))
        {
            return hr;
        }

        return mediaType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    }

    HRESULT WriteAudioSample(
        IMFSinkWriter* sinkWriter,
        DWORD streamIndex,
        const CapturedAudioPacket& packet)
    {
        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        HRESULT hr = MFCreateMemoryBuffer(static_cast<DWORD>(packet.samples.size()), &mediaBuffer);
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

        memcpy(destination, packet.samples.data(), packet.samples.size());
        mediaBuffer->Unlock();
        mediaBuffer->SetCurrentLength(static_cast<DWORD>(packet.samples.size()));

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

        hr = sample->SetSampleTime(packet.sampleTimeHns);
        if (FAILED(hr))
        {
            return hr;
        }

        hr = sample->SetSampleDuration(packet.durationHns);
        if (FAILED(hr))
        {
            return hr;
        }

        return sinkWriter->WriteSample(streamIndex, sample.Get());
    }
}
