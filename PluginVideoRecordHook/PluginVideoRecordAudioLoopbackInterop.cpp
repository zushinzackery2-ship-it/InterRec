#include "pch.h"

#include "PluginVideoRecordAudioLoopbackInterop.h"

namespace
{
    class AudioInterfaceActivator
        : public IActivateAudioInterfaceCompletionHandler
        , public IAgileObject
    {
    public:
        explicit AudioInterfaceActivator(HANDLE completionEvent)
            : referenceCount_(1)
            , completionEvent_(completionEvent)
            , activationResult_(E_FAIL)
        {
        }

        STDMETHODIMP QueryInterface(REFIID riid, void** object) override
        {
            if (!object)
            {
                return E_POINTER;
            }

            if (riid == __uuidof(IUnknown) ||
                riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
            {
                *object = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            }
            else if (riid == __uuidof(IAgileObject))
            {
                *object = static_cast<IAgileObject*>(this);
            }
            else
            {
                *object = nullptr;
                return E_NOINTERFACE;
            }

            AddRef();
            return S_OK;
        }

        STDMETHODIMP_(ULONG) AddRef() override
        {
            return static_cast<ULONG>(InterlockedIncrement(&referenceCount_));
        }

        STDMETHODIMP_(ULONG) Release() override
        {
            ULONG count = static_cast<ULONG>(InterlockedDecrement(&referenceCount_));
            if (count == 0)
            {
                delete this;
            }

            return count;
        }

        STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) override
        {
            HRESULT activateResult = E_FAIL;
            Microsoft::WRL::ComPtr<IUnknown> unknownAudioClient;
            HRESULT hr = operation->GetActivateResult(&activateResult, &unknownAudioClient);
            if (SUCCEEDED(hr) && SUCCEEDED(activateResult) && unknownAudioClient)
            {
                activationResult_ = unknownAudioClient.As(&audioClient_);
            }
            else if (FAILED(hr))
            {
                activationResult_ = hr;
            }
            else
            {
                activationResult_ = activateResult;
            }

            SetEvent(completionEvent_);
            return S_OK;
        }

        HRESULT GetAudioClient(Microsoft::WRL::ComPtr<IAudioClient>& audioClient) const
        {
            audioClient = audioClient_;
            return activationResult_;
        }

    private:
        ~AudioInterfaceActivator() = default;

        LONG referenceCount_;
        HANDLE completionEvent_;
        HRESULT activationResult_;
        Microsoft::WRL::ComPtr<IAudioClient> audioClient_;
    };
}

namespace PluginVideoRecord
{
    HRESULT ActivateProcessLoopbackAudioClient(
        DWORD processId,
        Microsoft::WRL::ComPtr<IAudioClient>& audioClient)
    {
        HANDLE activateEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!activateEvent)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        AUDIOCLIENT_ACTIVATION_PARAMS activationParams = {};
        activationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activationParams.ProcessLoopbackParams.ProcessLoopbackMode =
            PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        activationParams.ProcessLoopbackParams.TargetProcessId = processId;

        PROPVARIANT activateParameters = {};
        activateParameters.vt = VT_BLOB;
        activateParameters.blob.cbSize = sizeof(activationParams);
        activateParameters.blob.pBlobData = reinterpret_cast<BYTE*>(&activationParams);

        Microsoft::WRL::ComPtr<IActivateAudioInterfaceAsyncOperation> asyncOperation;
        AudioInterfaceActivator* activator = new AudioInterfaceActivator(activateEvent);
        HRESULT hr = ActivateAudioInterfaceAsync(
            VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient),
            &activateParameters,
            activator,
            &asyncOperation);
        if (FAILED(hr))
        {
            activator->Release();
            CloseHandle(activateEvent);
            return hr;
        }

        if (WaitForSingleObject(activateEvent, 10000) != WAIT_OBJECT_0)
        {
            activator->Release();
            CloseHandle(activateEvent);
            return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        }

        hr = activator->GetAudioClient(audioClient);
        activator->Release();
        CloseHandle(activateEvent);
        return hr;
    }

    WAVEFORMATEX BuildLoopbackCaptureFormat()
    {
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(DefaultAudioChannels);
        format.nSamplesPerSec = DefaultAudioSampleRate;
        format.wBitsPerSample = static_cast<WORD>(DefaultAudioBitsPerSample);
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        return format;
    }
}
