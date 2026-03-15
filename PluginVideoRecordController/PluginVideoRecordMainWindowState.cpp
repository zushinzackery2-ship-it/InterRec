#include "pch.h"

#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordPreviewRenderer.h"

namespace
{
    std::wstring GetBackendText(PluginVideoRecord::GraphicsBackend backend)
    {
        switch (backend)
        {
        case PluginVideoRecord::GraphicsBackend::Dx11:
            return L"DX11";

        case PluginVideoRecord::GraphicsBackend::Dx12:
            return L"DX12";

        default:
            return L"未识别";
        }
    }

    std::wstring GetRecorderText(PluginVideoRecord::RecorderState recorderState)
    {
        switch (recorderState)
        {
        case PluginVideoRecord::RecorderState::Recording:
            return L"录制中";

        case PluginVideoRecord::RecorderState::Error:
            return L"错误";

        default:
            return L"待机中";
        }
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordMainWindow::PaintPreview()
    {
        if (GetSelectedPage() != ControllerPage::Preview)
        {
            PAINTSTRUCT paintStruct = {};
            BeginPaint(hwnd_, &paintStruct);
            EndPaint(hwnd_, &paintStruct);
            return;
        }

        PaintControllerPreview(hwnd_, previewRect_, preview_, isOnline_, recorderState_ == RecorderState::Recording);
    }

    void PluginVideoRecordMainWindow::RefreshStatus()
    {
        SharedState state = {};
        ResetSharedState(state);
        ipc_.ReadState(state);

        bool isOnline = state.protocolVersion == ProtocolVersion &&
            state.connectionState == static_cast<LONG>(ConnectionState::Online) &&
            static_cast<ULONGLONG>(state.heartbeatTickCount) + OfflineTimeoutMs >= GetTickCount64();

        UpdateStatusControls(state, isOnline);
        UpdatePreview(isOnline && recorderState_ == RecorderState::Recording);
    }

    void PluginVideoRecordMainWindow::RefreshLogs()
    {
        std::vector<std::wstring> logLines;
        if (!ipc_.ReadLogs(logLines))
        {
            return;
        }

        for (const std::wstring& line : logLines)
        {
            AppendLogLine(line);
        }
    }

    void PluginVideoRecordMainWindow::UpdatePreview(bool shouldReadPreview)
    {
        if (shouldReadPreview)
        {
            ipc_.ReadPreview(preview_);
        }
        else
        {
            preview_.isValid = false;
            preview_.width = 0;
            preview_.height = 0;
            preview_.pixels.clear();
        }

        if (GetSelectedPage() == ControllerPage::Preview)
        {
            InvalidateRect(hwnd_, &previewRect_, FALSE);
        }
    }

    void PluginVideoRecordMainWindow::UpdateStatusControls(const SharedState& state, bool isOnline)
    {
        isOnline_ = isOnline;
        recorderState_ = static_cast<RecorderState>(state.recorderState);
        graphicsBackend_ = static_cast<GraphicsBackend>(state.graphicsBackend);

        SetControlText(communicationValue_, isOnline_ ? L"正常" : L"离线");
        SetControlText(backendValue_, GetBackendText(graphicsBackend_));
        SetControlText(recorderValue_, GetRecorderText(recorderState_));

        std::wstring outputText = state.currentOutputPath[0] != L'\0'
            ? state.currentOutputPath
            : L"\\PluginVideoRecord\\时间戳.mp4";
        SetControlText(outputEdit_, outputText);

        std::wstring messageText;
        if (recorderState_ == RecorderState::Error)
        {
            messageText = state.lastErrorMessage[0] != L'\0' ? state.lastErrorMessage : L"录制失败。";
        }
        else if (!isOnline_)
        {
            messageText = L"Hook 端未在线。";
        }
        else if (recorderState_ == RecorderState::Recording)
        {
            messageText = L"录制进行中。";
        }
        else
        {
            messageText = L"等待录制命令。";
        }

        SetControlText(messageEdit_, messageText);

        EnableWindow(startButton_, isOnline_ && recorderState_ != RecorderState::Recording);
        EnableWindow(stopButton_, isOnline_ && recorderState_ == RecorderState::Recording);
    }

    void PluginVideoRecordMainWindow::AppendLogLine(const std::wstring& text)
    {
        if (!logEdit_ || text.empty())
        {
            return;
        }

        std::wstring line = text + L"\r\n";
        SendMessageW(logEdit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(logEdit_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    }
}
