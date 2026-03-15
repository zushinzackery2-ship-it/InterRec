#include "pch.h"

#include "PluginVideoRecordMainWindow.h"

namespace
{
    constexpr UINT_PTR RefreshTimerId = 1;
    constexpr UINT RefreshIntervalMs = 100;
    constexpr int StartButtonId = 1001;
    constexpr int StopButtonId = 1002;
    constexpr int MinWindowWidth = 600;
    constexpr int MinWindowHeight = 440;
    constexpr wchar_t WindowClassName[] = L"PluginVideoRecordControllerWindow";
}

namespace PluginVideoRecord
{
    PluginVideoRecordMainWindow::PluginVideoRecordMainWindow()
        : hwnd_(nullptr)
        , tabControl_(nullptr)
        , titleLabel_(nullptr)
        , communicationCaption_(nullptr)
        , communicationValue_(nullptr)
        , backendCaption_(nullptr)
        , backendValue_(nullptr)
        , recorderCaption_(nullptr)
        , recorderValue_(nullptr)
        , outputCaption_(nullptr)
        , outputEdit_(nullptr)
        , messageCaption_(nullptr)
        , messageEdit_(nullptr)
        , startButton_(nullptr)
        , stopButton_(nullptr)
        , logEdit_(nullptr)
        , regularFont_(nullptr)
        , titleFont_(nullptr)
        , monoFont_(nullptr)
        , isOnline_(false)
        , recorderState_(RecorderState::Standby)
        , graphicsBackend_(GraphicsBackend::Unknown)
    {
        SetRectEmpty(&pageContentRect_);
        SetRectEmpty(&previewRect_);
        preview_.isValid = false;
        preview_.width = 0;
        preview_.height = 0;
    }

    PluginVideoRecordMainWindow::~PluginVideoRecordMainWindow()
    {
        DestroyFonts();
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
        INITCOMMONCONTROLSEX controls = {};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&controls);

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
            L"InterRec Controller",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            640,
            520,
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
            if (!self->CreateFonts())
            {
                return -1;
            }
            self->CreateControls();
            self->LayoutControls();
            self->ApplyPageVisibility();
            self->AppendLogLine(L"[UI] 控制器已启动。");
            SetTimer(hwnd, RefreshTimerId, RefreshIntervalMs, nullptr);
            self->RefreshStatus();
            self->RefreshLogs();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case StartButtonId:
                self->AppendLogLine(
                    self->ipc_.SendCommand(CommandType::StartRecording)
                        ? L"[UI] 已发送开始录制命令。"
                        : L"[UI] 发送开始录制命令失败。");
                self->RefreshStatus();
                return 0;

            case StopButtonId:
                self->AppendLogLine(
                    self->ipc_.SendCommand(CommandType::StopRecording)
                        ? L"[UI] 已发送停止录制命令。"
                        : L"[UI] 发送停止录制命令失败。");
                self->RefreshStatus();
                return 0;

            default:
                break;
            }
            break;

        case WM_NOTIFY:
            if (reinterpret_cast<LPNMHDR>(lParam)->hwndFrom == self->tabControl_ &&
                reinterpret_cast<LPNMHDR>(lParam)->code == TCN_SELCHANGE)
            {
                self->ApplyPageVisibility();
                return 0;
            }
            break;

        case WM_SIZE:
            self->LayoutControls();
            self->ApplyPageVisibility();
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = MinWindowWidth;
            minMaxInfo->ptMinTrackSize.y = MinWindowHeight;
            return 0;
        }

        case WM_TIMER:
            if (wParam == RefreshTimerId)
            {
                self->RefreshStatus();
                self->RefreshLogs();
                return 0;
            }
            break;

        case WM_PAINT:
            self->PaintPreview();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, RefreshTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
