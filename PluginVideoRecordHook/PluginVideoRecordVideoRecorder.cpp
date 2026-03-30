#include "pch.h"

#include "PluginVideoRecordPreviewPublisher.h"
#include "PluginVideoRecordVideoRecorder.h"

namespace
{
    std::wstring GetPathLeafName(const std::wstring& path)
    {
        const size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return path;
        }

        return path.substr(separator + 1);
    }

    bool RemoveLastPathComponent(std::wstring& path)
    {
        const size_t separator = path.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return false;
        }

        path.resize(separator);
        return !path.empty();
    }

    bool IsPathLeafEqual(const std::wstring& path, const wchar_t* expectedName)
    {
        return _wcsicmp(GetPathLeafName(path).c_str(), expectedName) == 0;
    }

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

        // 一些游戏把主程序放在 Binaries 或 Binaries\Win64 目录下，录屏目录放回游戏根目录更直观。
        std::wstring rootDirectory = gameDirectory;
        if (IsPathLeafEqual(rootDirectory, L"Win64") ||
            IsPathLeafEqual(rootDirectory, L"Win32") ||
            IsPathLeafEqual(rootDirectory, L"x64") ||
            IsPathLeafEqual(rootDirectory, L"x86"))
        {
            std::wstring parentDirectory = rootDirectory;
            if (RemoveLastPathComponent(parentDirectory) && IsPathLeafEqual(parentDirectory, L"Binaries"))
            {
                rootDirectory = parentDirectory;
                RemoveLastPathComponent(rootDirectory);
            }
        }
        else if (IsPathLeafEqual(rootDirectory, L"Binaries"))
        {
            RemoveLastPathComponent(rootDirectory);
        }

        if (!rootDirectory.empty())
        {
            gameDirectory = rootDirectory;
        }

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
}

namespace PluginVideoRecord
{
    PluginVideoRecordVideoRecorder::PluginVideoRecordVideoRecorder()
        : startQpcHns_(0)
        , qpcReady_(false)
        , recording_(false)
        , activeBackend_(GraphicsBackend::Unknown)
        , previewPublisher_(nullptr)
    {
        performanceFrequency_.QuadPart = 0;
        startCounter_.QuadPart = 0;
    }

    PluginVideoRecordVideoRecorder::~PluginVideoRecordVideoRecorder()
    {
        Stop();
    }

    void PluginVideoRecordVideoRecorder::SetPreviewPublisher(PluginVideoRecordPreviewPublisher* previewPublisher)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previewPublisher_ = previewPublisher;
    }

    void PluginVideoRecordVideoRecorder::Stop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StopUnlocked();
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

    std::wstring PluginVideoRecordVideoRecorder::GetPendingOutputPath() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::wstring outputPath;
        if (BuildPendingOutputPath(outputPath))
        {
            return outputPath;
        }

        return L"";
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

    bool PluginVideoRecordVideoRecorder::BuildPendingOutputPath(std::wstring& outputPath) const
    {
        std::wstring gameDirectory;
        if (!GetGameDirectory(gameDirectory))
        {
            return false;
        }

        outputPath = gameDirectory + L"\\PluginVideoRecord\\时间戳.mp4";
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

    bool PluginVideoRecordVideoRecorder::StartWriterAndAudio(
        UINT width,
        UINT height,
        const std::wstring& outputPath,
        std::wstring& error)
    {
        if (!QueryPerformanceFrequency(&performanceFrequency_) || !QueryPerformanceCounter(&startCounter_))
        {
            error = L"无法初始化高精度计时器。";
            return false;
        }

        startQpcHns_ = startCounter_.QuadPart * 10000000LL / performanceFrequency_.QuadPart;
        qpcReady_ = true;

        if (!writer_.Start(outputPath, width, height, error))
        {
            qpcReady_ = false;
            return false;
        }

        if (!audioCapture_.Start(GetCurrentProcessId(), startQpcHns_, &writer_, error))
        {
            writer_.Stop();
            qpcReady_ = false;
            return false;
        }

        return true;
    }

    void PluginVideoRecordVideoRecorder::StopUnlocked()
    {
        audioCapture_.Stop();
        writer_.Stop();
        dx11Capture_.Shutdown();
        dx12Capture_.Shutdown();
        vulkanCapture_.Shutdown();

        startQpcHns_ = 0;
        qpcReady_ = false;
        recording_ = false;
        activeBackend_ = GraphicsBackend::Unknown;
        currentOutputPath_.clear();

        if (previewPublisher_)
        {
            previewPublisher_->Clear();
        }
    }
}
