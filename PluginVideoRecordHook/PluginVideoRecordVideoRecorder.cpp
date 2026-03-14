#include "pch.h"

#include "PluginVideoRecordVideoRecorder.h"

namespace
{
    bool GetGameDirectory(std::wstring& gameDirectory)
    {
        wchar_t modulePath[MAX_PATH] = {};
        DWORD length = GetModuleFileNameW(nullptr, modulePath, _countof(modulePath));
        if (length == 0 || length >= _countof(modulePath))
        {
            return false;
        }

        if (!PathRemoveFileSpecW(modulePath))
        {
            return false;
        }

        gameDirectory = modulePath;
        return true;
    }

    std::wstring BuildTimestamp()
    {
        SYSTEMTIME localTime = {};
        GetLocalTime(&localTime);

        wchar_t buffer[64] = {};
        swprintf_s(
            buffer,
            L"%04u%02u%02u_%02u%02u%02u",
            localTime.wYear,
            localTime.wMonth,
            localTime.wDay,
            localTime.wHour,
            localTime.wMinute,
            localTime.wSecond);
        return buffer;
    }

    LONGLONG ConvertCounterToHns(LONGLONG counter, LONGLONG frequency)
    {
        if (frequency == 0)
        {
            return 0;
        }

        const LONGLONG quotient = counter / frequency;
        const LONGLONG remainder = counter % frequency;
        return quotient * 10000000LL + remainder * 10000000LL / frequency;
    }
}

namespace PluginVideoRecord
{
    PluginVideoRecordVideoRecorder::PluginVideoRecordVideoRecorder()
        : startQpcHns_(0)
        , qpcReady_(false)
        , recording_(false)
    {
        performanceFrequency_.QuadPart = 0;
        startCounter_.QuadPart = 0;
    }

    PluginVideoRecordVideoRecorder::~PluginVideoRecordVideoRecorder()
    {
        Stop();
    }

    bool PluginVideoRecordVideoRecorder::Start(
        const RainGuiDx11HookRuntime* runtime,
        std::wstring& outputPath,
        std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();

        if (!runtime || !runtime->swapChain || !runtime->device || !runtime->deviceContext)
        {
            error = L"DX11 运行时还没准备好，暂时不能开始录制。";
            return false;
        }

        if (!capture_.Initialize(runtime, error))
        {
            return false;
        }

        if (!BuildOutputPath(outputPath, error))
        {
            capture_.Shutdown();
            return false;
        }

        if (!QueryPerformanceFrequency(&performanceFrequency_) || !QueryPerformanceCounter(&startCounter_))
        {
            error = L"无法初始化高精度计时器。";
            capture_.Shutdown();
            return false;
        }

        startQpcHns_ = ConvertCounterToHns(startCounter_.QuadPart, performanceFrequency_.QuadPart);
        qpcReady_ = true;

        if (!writer_.Start(outputPath, capture_.GetCaptureWidth(), capture_.GetCaptureHeight(), error))
        {
            capture_.Shutdown();
            qpcReady_ = false;
            startQpcHns_ = 0;
            return false;
        }

        if (!audioCapture_.Start(GetCurrentProcessId(), startQpcHns_, &writer_, error))
        {
            writer_.Stop();
            capture_.Shutdown();
            qpcReady_ = false;
            startQpcHns_ = 0;
            return false;
        }

        currentOutputPath_ = outputPath;
        recording_ = true;
        return true;
    }

    void PluginVideoRecordVideoRecorder::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();
    }

    bool PluginVideoRecordVideoRecorder::OnFrame(const RainGuiDx11HookRuntime* runtime, std::wstring& error)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!recording_)
        {
            return true;
        }

        if (audioCapture_.TryGetLastError(error))
        {
            return false;
        }

        if (!capture_.MatchesRuntime(runtime))
        {
            error = L"检测到分辨率或缓冲格式变化，当前录制已停止。";
            return false;
        }

        CapturedFrame frame = {};
        if (!capture_.CaptureFrame(runtime, GetSampleTimeHns(), frame, error))
        {
            return false;
        }

        if (!writer_.EnqueueFrame(std::move(frame)))
        {
            std::wstring writerError;
            if (writer_.TryGetLastError(writerError))
            {
                error = writerError;
                return false;
            }
        }

        return true;
    }

    bool PluginVideoRecordVideoRecorder::IsRecording() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return recording_;
    }

    std::wstring PluginVideoRecordVideoRecorder::GetCurrentOutputPath() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return currentOutputPath_;
    }

    bool PluginVideoRecordVideoRecorder::BuildOutputPath(std::wstring& outputPath, std::wstring& error) const
    {
        std::wstring gameDirectory;
        if (!GetGameDirectory(gameDirectory))
        {
            error = L"无法定位游戏目录。";
            return false;
        }

        std::wstring outputDirectory = gameDirectory + L"\\PluginVideoRecord";
        if (!CreateDirectoryW(outputDirectory.c_str(), nullptr))
        {
            DWORD lastError = GetLastError();
            if (lastError != ERROR_ALREADY_EXISTS)
            {
                error = L"无法创建 PluginVideoRecord 输出目录。";
                return false;
            }
        }

        outputPath = outputDirectory + L"\\" + BuildTimestamp() + L".mp4";
        return true;
    }

    LONGLONG PluginVideoRecordVideoRecorder::GetSampleTimeHns() const
    {
        if (!qpcReady_ || performanceFrequency_.QuadPart == 0)
        {
            return 0;
        }

        LARGE_INTEGER currentCounter = {};
        QueryPerformanceCounter(&currentCounter);

        LONGLONG delta = currentCounter.QuadPart - startCounter_.QuadPart;
        return delta * 10000000LL / performanceFrequency_.QuadPart;
    }

    void PluginVideoRecordVideoRecorder::StopUnlocked()
    {
        audioCapture_.Stop();
        writer_.Stop();
        capture_.Shutdown();

        startQpcHns_ = 0;
        qpcReady_ = false;
        recording_ = false;
        currentOutputPath_.clear();
    }
}
