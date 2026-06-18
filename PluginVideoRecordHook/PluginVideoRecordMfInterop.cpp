#include "pch.h"

#include "PluginVideoRecordMfInternal.h"
#include "PluginVideoRecordMfInterop.h"
#include "PluginVideoRecordMfWriter.h"

namespace
{
    class VectorMediaBuffer final : public IMFMediaBuffer
    {
    public:
        explicit VectorMediaBuffer(std::vector<std::uint8_t>&& data)
            : refCount_(1)
            , data_(std::move(data))
            , currentLength_(static_cast<DWORD>(data_.size()))
        {
        }

        STDMETHODIMP QueryInterface(REFIID riid, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaBuffer))
            {
                *object = static_cast<IMFMediaBuffer*>(this);
                AddRef();
                return S_OK;
            }

            *object = nullptr;
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&refCount_));
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG refCount = static_cast<ULONG>(InterlockedDecrement(&refCount_));
            if (refCount == 0)
            {
                delete this;
            }

            return refCount;
        }

        STDMETHODIMP Lock(BYTE** buffer, DWORD* maxLength, DWORD* currentLength) override
        {
            if (!buffer)
            {
                return E_POINTER;
            }

            *buffer = data_.data();
            if (maxLength)
            {
                *maxLength = static_cast<DWORD>(data_.size());
            }

            if (currentLength)
            {
                *currentLength = currentLength_;
            }

            return S_OK;
        }

        STDMETHODIMP Unlock() override
        {
            return S_OK;
        }

        STDMETHODIMP GetCurrentLength(DWORD* currentLength) override
        {
            if (!currentLength)
            {
                return E_POINTER;
            }

            *currentLength = currentLength_;
            return S_OK;
        }

        STDMETHODIMP SetCurrentLength(DWORD currentLength) override
        {
            if (currentLength > data_.size())
            {
                return E_INVALIDARG;
            }

            currentLength_ = currentLength;
            return S_OK;
        }

        STDMETHODIMP GetMaxLength(DWORD* maxLength) override
        {
            if (!maxLength)
            {
                return E_POINTER;
            }

            *maxLength = static_cast<DWORD>(data_.size());
            return S_OK;
        }

    private:
        volatile LONG refCount_;
        std::vector<std::uint8_t> data_;
        DWORD currentLength_;
    };

    HRESULT CreateVectorMediaBuffer(
        std::vector<std::uint8_t>&& pixels,
        Microsoft::WRL::ComPtr<IMFMediaBuffer>& mediaBuffer)
    {
        if (pixels.empty() || pixels.size() > MAXDWORD)
        {
            return E_INVALIDARG;
        }

        VectorMediaBuffer* buffer = new (std::nothrow) VectorMediaBuffer(std::move(pixels));
        if (!buffer)
        {
            return E_OUTOFMEMORY;
        }

        mediaBuffer.Attach(buffer);
        return S_OK;
    }
}

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
        CapturedFrame&& frame,
        LONGLONG duration)
    {
        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        HRESULT hr = CreateVectorMediaBuffer(std::move(frame.pixels), mediaBuffer);
        if (FAILED(hr))
        {
            return hr;
        }

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
