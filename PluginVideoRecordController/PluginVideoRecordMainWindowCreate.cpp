#include "pch.h"

#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordMainWindowInternal.h"

namespace PluginVideoRecord
{
    bool PluginVideoRecordMainWindow::CreateFonts()
    {
        regularFont_ = CreateFontW(
            MainWindowInternal::ScaleFontHeight(-13),
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Microsoft YaHei UI");
        smallFont_ = CreateFontW(
            MainWindowInternal::ScaleFontHeight(-11),
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Microsoft YaHei UI");
        titleFont_ = CreateFontW(
            MainWindowInternal::ScaleFontHeight(-20),
            0,
            0,
            0,
            FW_SEMIBOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH,
            L"Microsoft YaHei UI");
        monoFont_ = CreateFontW(
            MainWindowInternal::ScaleFontHeight(-12),
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            FIXED_PITCH,
            L"Consolas");
        return regularFont_ && smallFont_ && titleFont_ && monoFont_;
    }

    void PluginVideoRecordMainWindow::CreateControls()
    {
        tabControl_ = CreateWindowExW(
            0,
            WC_TABCONTROLW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,
            0,
            100,
            100,
            hwnd_,
            nullptr,
            nullptr,
            nullptr);

        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"录制");
        TabCtrl_InsertItem(tabControl_, static_cast<int>(ControllerPage::Record), &item);
        item.pszText = const_cast<wchar_t*>(L"预览");
        TabCtrl_InsertItem(tabControl_, static_cast<int>(ControllerPage::Preview), &item);
        item.pszText = const_cast<wchar_t*>(L"日志");
        TabCtrl_InsertItem(tabControl_, static_cast<int>(ControllerPage::Log), &item);

        titleLabel_ = CreateWindowExW(0, L"STATIC", L"InterRec", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        communicationCaption_ = CreateWindowExW(0, L"STATIC", L"通信", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        communicationValue_ = CreateWindowExW(0, L"STATIC", L"离线", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        backendCaption_ = CreateWindowExW(0, L"STATIC", L"当前可录制后端", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        backendValue_ = CreateWindowExW(0, L"STATIC", L"未识别", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        recordBackendCaption_ = CreateWindowExW(0, L"STATIC", L"录制后端", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        backendDx11Radio_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"DX11",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::BackendDx11RadioId)),
            nullptr,
            nullptr);
        backendDx12Radio_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"DX12",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::BackendDx12RadioId)),
            nullptr,
            nullptr);
        backendVulkanRadio_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"Vulkan",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::BackendVulkanRadioId)),
            nullptr,
            nullptr);
        recorderCaption_ = CreateWindowExW(0, L"STATIC", L"状态", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        recorderValue_ = CreateWindowExW(0, L"STATIC", L"待机中", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        vulkanCaption_ = CreateWindowExW(0, L"STATIC", L"Vulkan-Layer模式", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        vulkanDescription_ = CreateWindowExW(0, L"STATIC", L"开启后可识别Vulkan后端", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        vulkanHint_ = CreateWindowExW(0, L"STATIC", L"目标进程重启后生效", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        vulkanEnabledCheck_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"启用",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::VulkanEnhancedCheckId)),
            nullptr,
            nullptr);
        outputCaption_ = CreateWindowExW(0, L"STATIC", L"输出路径", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        outputEdit_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_PATHELLIPSIS, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        messageCaption_ = CreateWindowExW(0, L"STATIC", L"状态信息", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        messageEdit_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        startButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"开始录制",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::StartButtonId)),
            nullptr,
            nullptr);
        stopButton_ = CreateWindowExW(
            0,
            L"BUTTON",
            L"结束录制",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0,
            0,
            0,
            0,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(MainWindowInternal::StopButtonId)),
            nullptr,
            nullptr);
        previewPanel_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            MainWindowInternal::PreviewPanelClassName,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
            this);
        logEdit_ = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL,
            0,
            0,
            0,
            0,
            hwnd_,
            nullptr,
            nullptr,
            nullptr);

        SetControlFont(tabControl_, regularFont_);
        SetControlFont(titleLabel_, titleFont_);
        SetControlFont(communicationCaption_, regularFont_);
        SetControlFont(communicationValue_, regularFont_);
        SetControlFont(backendCaption_, regularFont_);
        SetControlFont(backendValue_, regularFont_);
        SetControlFont(recordBackendCaption_, regularFont_);
        SetControlFont(backendDx11Radio_, regularFont_);
        SetControlFont(backendDx12Radio_, regularFont_);
        SetControlFont(backendVulkanRadio_, regularFont_);
        SetControlFont(recorderCaption_, regularFont_);
        SetControlFont(recorderValue_, regularFont_);
        SetControlFont(vulkanCaption_, regularFont_);
        SetControlFont(vulkanDescription_, smallFont_);
        SetControlFont(vulkanHint_, smallFont_);
        SetControlFont(vulkanEnabledCheck_, regularFont_);
        SetControlFont(outputCaption_, regularFont_);
        SetControlFont(outputEdit_, regularFont_);
        SetControlFont(messageCaption_, regularFont_);
        SetControlFont(messageEdit_, regularFont_);
        SetControlFont(startButton_, regularFont_);
        SetControlFont(stopButton_, regularFont_);
        SetControlFont(logEdit_, monoFont_);
    }

    void PluginVideoRecordMainWindow::DestroyFonts()
    {
        if (monoFont_)
        {
            DeleteObject(monoFont_);
            monoFont_ = nullptr;
        }

        if (smallFont_)
        {
            DeleteObject(smallFont_);
            smallFont_ = nullptr;
        }

        if (titleFont_)
        {
            DeleteObject(titleFont_);
            titleFont_ = nullptr;
        }

        if (regularFont_)
        {
            DeleteObject(regularFont_);
            regularFont_ = nullptr;
        }
    }
}
