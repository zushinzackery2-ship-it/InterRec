#include "pch.h"

#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordMainWindowInternal.h"

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
        , recordBackendCaption_(nullptr)
        , backendDx11Radio_(nullptr)
        , backendDx12Radio_(nullptr)
        , backendVulkanRadio_(nullptr)
        , recorderCaption_(nullptr)
        , recorderValue_(nullptr)
        , vulkanCaption_(nullptr)
        , vulkanEnabledCheck_(nullptr)
        , vulkanDescription_(nullptr)
        , vulkanHint_(nullptr)
        , outputCaption_(nullptr)
        , outputEdit_(nullptr)
        , messageCaption_(nullptr)
        , messageEdit_(nullptr)
        , startButton_(nullptr)
        , stopButton_(nullptr)
        , logEdit_(nullptr)
        , previewPanel_(nullptr)
        , regularFont_(nullptr)
        , smallFont_(nullptr)
        , titleFont_(nullptr)
        , monoFont_(nullptr)
        , isOnline_(false)
        , recorderState_(RecorderState::Standby)
        , availableBackendMask_(GraphicsBackendMask_None)
        , selectedBackend_(GraphicsBackend::Unknown)
        , activeRecordingBackend_(GraphicsBackend::Unknown)
        , isVulkanLayerEnabled_(false)
        , isForceAutoHookEnabled_(false)
    {
        SetRectEmpty(&pageContentRect_);
        SetRectEmpty(&previewRect_);
        preview_.isValid = false;
        preview_.width = 0;
        preview_.height = 0;
        preview_.sampleTimeHns = 0;
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
        windowClass.lpszClassName = MainWindowInternal::WindowClassName;
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&windowClass))
        {
            return false;
        }

        WNDCLASSEXW previewPanelClass = {};
        previewPanelClass.cbSize = sizeof(previewPanelClass);
        previewPanelClass.hInstance = instanceHandle;
        previewPanelClass.lpszClassName = MainWindowInternal::PreviewPanelClassName;
        previewPanelClass.lpfnWndProc = PreviewPanelProc;
        previewPanelClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        previewPanelClass.hbrBackground = nullptr;
        if (!RegisterClassExW(&previewPanelClass))
        {
            return false;
        }

        hwnd_ = CreateWindowExW(
            0,
            MainWindowInternal::WindowClassName,
            L"InterRec Controller",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_SIZEBOX | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            MainWindowInternal::InitialWindowWidth,
            MainWindowInternal::InitialWindowHeight,
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
}
