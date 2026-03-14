#include "pch.h"

#include "PluginVideoRecordAudioLoopbackInterop.h"
#include "PluginVideoRecordMfInterop.h"
#include "PluginVideoRecordProcessAudioCapture.h"
namespace PluginVideoRecord
{
    PluginVideoRecordProcessAudioCapture::PluginVideoRecordProcessAudioCapture()
        : stopEvent_(nullptr)
        , processId_(0)
        , recordingStartQpcHns_(0)
        , writer_(nullptr)
        , running_(false)
        , stopRequested_(false)
        , startupCompleted_(false)
        , failed_(false)
    {
    }

    PluginVideoRecordProcessAudioCapture::~PluginVideoRecordProcessAudioCapture()
    {
        Stop();
    }
    bool PluginVideoRecordProcessAudioCapture::Start(
        DWORD processId,
        LONGLONG recordingStartQpcHns,
        PluginVideoRecordMfWriter* writer,
        std::wstring& error)
    {
        Stop();

        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_)
        {
            error = L"无法创建音频采集停止事件。";
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            processId_ = processId;
            recordingStartQpcHns_ = recordingStartQpcHns;
            writer_ = writer;
            running_ = true;
            stopRequested_ = false;
            startupCompleted_ = false;
            failed_ = false;
            lastError_.clear();
        }

        captureThread_ = std::thread(&PluginVideoRecordProcessAudioCapture::CaptureThread, this);

        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [this]() { return startupCompleted_; });
        if (failed_)
        {
            error = lastError_;
            lock.unlock();
            Stop();
            return false;
        }

        return true;
    }
    void PluginVideoRecordProcessAudioCapture::Stop()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_ && !captureThread_.joinable())
            {
                return;
            }

            stopRequested_ = true;
        }

        if (stopEvent_)
        {
            SetEvent(stopEvent_);
        }

        if (captureThread_.joinable())
        {
            captureThread_.join();
        }

        if (stopEvent_)
        {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        stopRequested_ = false;
        startupCompleted_ = false;
        writer_ = nullptr;
        processId_ = 0;
        recordingStartQpcHns_ = 0;
    }
    bool PluginVideoRecordProcessAudioCapture::TryGetLastError(std::wstring& error) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_)
        {
            return false;
        }

        error = lastError_;
        return true;
    }
    void PluginVideoRecordProcessAudioCapture::CaptureThread()
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr))
        {
            SetFailure(BuildMfError(L"无法初始化音频采集 COM。", hr));
            return;
        }

        Microsoft::WRL::ComPtr<IAudioClient> audioClient;
        hr = ActivateProcessLoopbackAudioClient(processId_, audioClient);
        if (FAILED(hr) || !audioClient)
        {
            SetFailure(BuildMfError(L"无法激活按进程音频 loopback 接口。", hr));
            CoUninitialize();
            return;
        }

        WAVEFORMATEX format = BuildLoopbackCaptureFormat();
        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0,
            0,
            &format,
            nullptr);
        if (FAILED(hr))
        {
            SetFailure(BuildMfError(L"初始化按进程音频 loopback 失败。", hr));
            CoUninitialize();
            return;
        }

        HANDLE sampleReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!sampleReadyEvent)
        {
            SetFailure(L"无法创建音频样本事件。");
            CoUninitialize();
            return;
        }

        hr = audioClient->SetEventHandle(sampleReadyEvent);
        if (FAILED(hr))
        {
            CloseHandle(sampleReadyEvent);
            SetFailure(BuildMfError(L"设置音频事件句柄失败。", hr));
            CoUninitialize();
            return;
        }

        Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient;
        hr = audioClient->GetService(IID_PPV_ARGS(&captureClient));
        if (FAILED(hr))
        {
            CloseHandle(sampleReadyEvent);
            SetFailure(BuildMfError(L"获取音频捕获服务失败。", hr));
            CoUninitialize();
            return;
        }

        hr = audioClient->Start();
        if (FAILED(hr))
        {
            CloseHandle(sampleReadyEvent);
            SetFailure(BuildMfError(L"启动按进程音频采集失败。", hr));
            CoUninitialize();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            startupCompleted_ = true;
            condition_.notify_all();
        }

        HANDLE waitHandles[2] = { stopEvent_, sampleReadyEvent };
        const UINT32 bytesPerFrame = format.nBlockAlign;
        while (WaitForMultipleObjects(_countof(waitHandles), waitHandles, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
        {
            for (;;)
            {
                UINT32 packetFrameCount = 0;
                hr = captureClient->GetNextPacketSize(&packetFrameCount);
                if (FAILED(hr))
                {
                    SetFailure(BuildMfError(L"查询音频包大小失败。", hr));
                    break;
                }

                if (packetFrameCount == 0)
                {
                    break;
                }

                BYTE* packetData = nullptr;
                UINT32 framesAvailable = 0;
                DWORD packetFlags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                hr = captureClient->GetBuffer(
                    &packetData,
                    &framesAvailable,
                    &packetFlags,
                    &devicePosition,
                    &qpcPosition);
                if (FAILED(hr))
                {
                    SetFailure(BuildMfError(L"读取音频缓冲失败。", hr));
                    break;
                }

                CapturedAudioPacket packet = {};
                packet.sampleTimeHns = qpcPosition > static_cast<UINT64>(recordingStartQpcHns_)
                    ? static_cast<LONGLONG>(qpcPosition - recordingStartQpcHns_)
                    : 0;
                packet.durationHns =
                    static_cast<LONGLONG>(framesAvailable) * 10000000LL / DefaultAudioSampleRate;
                packet.samples.resize(static_cast<size_t>(framesAvailable) * bytesPerFrame);

                if (packetFlags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    ZeroMemory(packet.samples.data(), packet.samples.size());
                }
                else
                {
                    CopyMemory(
                        packet.samples.data(),
                        packetData,
                        static_cast<size_t>(framesAvailable) * bytesPerFrame);
                }

                hr = captureClient->ReleaseBuffer(framesAvailable);
                if (FAILED(hr))
                {
                    SetFailure(BuildMfError(L"释放音频缓冲失败。", hr));
                    break;
                }

                if (writer_ && !writer_->EnqueueAudioPacket(std::move(packet)))
                {
                    std::wstring writerError;
                    if (writer_->TryGetLastError(writerError))
                    {
                        SetFailure(writerError);
                    }
                    else
                    {
                        SetFailure(L"写入器拒绝接收音频样本。");
                    }
                    break;
                }
            }

            std::lock_guard<std::mutex> lock(mutex_);
            if (failed_ || stopRequested_)
            {
                break;
            }
        }

        audioClient->Stop();
        CloseHandle(sampleReadyEvent);
        CoUninitialize();

        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        if (!startupCompleted_)
        {
            startupCompleted_ = true;
            condition_.notify_all();
        }
    }
    void PluginVideoRecordProcessAudioCapture::SetFailure(const std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed_ = true;
        lastError_ = error;
        startupCompleted_ = true;
        condition_.notify_all();

        if (stopEvent_)
        {
            SetEvent(stopEvent_);
        }
    }
}
