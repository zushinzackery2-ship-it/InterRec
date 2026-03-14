#include "pch.h"

#include "PluginVideoRecordMainWindow.h"

namespace
{
    constexpr UINT_PTR RefreshTimerId = 1;
    constexpr UINT RefreshIntervalMs = 500;
    constexpr int StartButtonId = 1001;
    constexpr int StopButtonId = 1002;
    constexpr wchar_t WindowClassName[] = L"PluginVideoRecordControllerWindow";
}

namespace PluginVideoRecord
{
    PluginVideoRecordMainWindow::PluginVideoRecordMainWindow()
        : hwnd_(nullptr)
        , communicationLabel_(nullptr)
        , recorderLabel_(nullptr)
        , startButton_(nullptr)
        , stopButton_(nullptr)
    {
    }

    int PluginVideoRecordMainWindow::Run(HINSTANCE instanceHandle, int showCommand)
    {
        if (!Create(instanceHandle, showCommand))
        {
            return 1;
        }

        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }

    bool PluginVideoRecordMainWindow::Create(HINSTANCE instanceHandle, int showCommand)
    {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = instanceHandle;
        windowClass.lpszClassName = WindowClassName;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&windowClass))
        {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            WindowClassName,
            L"PluginVideoRecord 控制台",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            320,
            180,
            nullptr,
            nullptr,
            instanceHandle,
            this);
        if (!hwnd_)
        {
            return false;
        }

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);
        return true;
    }

    LRESULT CALLBACK PluginVideoRecordMainWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<PluginVideoRecordMainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<PluginVideoRecordMainWindow*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }

        if (!self)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_CREATE:
            self->CreateControls();
            SetTimer(hwnd, RefreshTimerId, RefreshIntervalMs, nullptr);
            self->RefreshStatus();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case StartButtonId:
                self->ipc_.SendCommand(CommandType::StartRecording);
                self->RefreshStatus();
                return 0;

            case StopButtonId:
                self->ipc_.SendCommand(CommandType::StopRecording);
                self->RefreshStatus();
                return 0;

            default:
                break;
            }
            break;

        case WM_TIMER:
            if (wParam == RefreshTimerId)
            {
                self->RefreshStatus();
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd, RefreshTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void PluginVideoRecordMainWindow::CreateControls()
    {
        communicationLabel_ = CreateWindowExW(0, L"STATIC", L"通信：离线", WS_CHILD | WS_VISIBLE, 24, 24, 240, 24, hwnd_, nullptr, nullptr, nullptr);
        recorderLabel_ = CreateWindowExW(0, L"STATIC", L"状态：待机中", WS_CHILD | WS_VISIBLE, 24, 56, 240, 24, hwnd_, nullptr, nullptr, nullptr);
        startButton_ = CreateWindowExW(0, L"BUTTON", L"开始录制", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24, 96, 112, 32, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(StartButtonId)), nullptr, nullptr);
        stopButton_ = CreateWindowExW(0, L"BUTTON", L"结束录制", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 152, 96, 112, 32, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(StopButtonId)), nullptr, nullptr);

        HFONT fontHandle = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        SendMessageW(communicationLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHandle), TRUE);
        SendMessageW(recorderLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHandle), TRUE);
        SendMessageW(startButton_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHandle), TRUE);
        SendMessageW(stopButton_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHandle), TRUE);
    }

    void PluginVideoRecordMainWindow::RefreshStatus()
    {
        SharedState state = {};
        ResetSharedState(state);
        ipc_.ReadState(state);

        bool isOnline = state.protocolVersion == ProtocolVersion &&
            state.connectionState == static_cast<LONG>(ConnectionState::Online) &&
            static_cast<ULONGLONG>(state.heartbeatTickCount) + OfflineTimeoutMs >= GetTickCount64();

        RecorderState recorderState = static_cast<RecorderState>(state.recorderState);
        bool isRecording = recorderState == RecorderState::Recording;

        UpdateLabel(communicationLabel_, isOnline ? L"通信：正常" : L"通信：离线");
        UpdateLabel(recorderLabel_, isRecording ? L"状态：录制中" : L"状态：待机中");

        EnableWindow(startButton_, isOnline && !isRecording);
        EnableWindow(stopButton_, isOnline && isRecording);
    }

    void PluginVideoRecordMainWindow::UpdateLabel(HWND controlHandle, const std::wstring& text) const
    {
        SetWindowTextW(controlHandle, text.c_str());
    }
}
