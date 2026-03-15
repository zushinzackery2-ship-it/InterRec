#include "pch.h"

#include "PluginVideoRecordMainWindow.h"

namespace
{
    constexpr int TabMargin = 16;
    constexpr int ContentInset = 20;
    constexpr int ButtonWidth = 112;
    constexpr int ButtonHeight = 34;
    constexpr int TitleHeight = 36;
    constexpr int LabelHeight = 24;
    constexpr int RowGap = 14;
    constexpr int ValueHeight = 28;
    constexpr int GroupGap = 12;

    int ClampInt(int value, int minimum, int maximum)
    {
        if (value < minimum)
        {
            return minimum;
        }

        if (value > maximum)
        {
            return maximum;
        }

        return value;
    }
}

namespace PluginVideoRecord
{
    bool PluginVideoRecordMainWindow::CreateFonts()
    {
        regularFont_ = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        titleFont_ = CreateFontW(-30, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        monoFont_ = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
        return regularFont_ && titleFont_ && monoFont_;
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
        backendCaption_ = CreateWindowExW(0, L"STATIC", L"后端", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        backendValue_ = CreateWindowExW(0, L"STATIC", L"未识别", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        recorderCaption_ = CreateWindowExW(0, L"STATIC", L"状态", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        recorderValue_ = CreateWindowExW(0, L"STATIC", L"待机中", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        outputCaption_ = CreateWindowExW(0, L"STATIC", L"输出路径", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        outputEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        messageCaption_ = CreateWindowExW(0, L"STATIC", L"状态信息", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        messageEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
            0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);
        startButton_ = CreateWindowExW(0, L"BUTTON", L"开始录制", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1001)), nullptr, nullptr);
        stopButton_ = CreateWindowExW(0, L"BUTTON", L"结束录制", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1002)), nullptr, nullptr);
        logEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL,
            0, 0, 0, 0, hwnd_, nullptr, nullptr, nullptr);

        SetControlFont(tabControl_, regularFont_);
        SetControlFont(titleLabel_, titleFont_);
        SetControlFont(communicationCaption_, regularFont_);
        SetControlFont(communicationValue_, regularFont_);
        SetControlFont(backendCaption_, regularFont_);
        SetControlFont(backendValue_, regularFont_);
        SetControlFont(recorderCaption_, regularFont_);
        SetControlFont(recorderValue_, regularFont_);
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

    void PluginVideoRecordMainWindow::LayoutControls()
    {
        RECT clientRect = {};
        GetClientRect(hwnd_, &clientRect);

        const int clientWidth = ClampInt(clientRect.right - clientRect.left, 0, 32767);
        const int clientHeight = ClampInt(clientRect.bottom - clientRect.top, 0, 32767);
        MoveWindow(
            tabControl_,
            TabMargin,
            TabMargin,
            ClampInt(clientWidth - TabMargin * 2, 0, 32767),
            ClampInt(clientHeight - TabMargin * 2, 0, 32767),
            TRUE);

        RECT tabRect = {};
        GetClientRect(tabControl_, &tabRect);
        TabCtrl_AdjustRect(tabControl_, FALSE, &tabRect);
        MapWindowPoints(tabControl_, hwnd_, reinterpret_cast<LPPOINT>(&tabRect), 2);
        pageContentRect_ = tabRect;

        const int left = pageContentRect_.left + ContentInset;
        const int top = pageContentRect_.top + ContentInset;
        const int right = pageContentRect_.right - ContentInset;
        const int bottom = pageContentRect_.bottom - ContentInset;
        const int contentWidth = ClampInt(right - left, 0, 32767);
        const int contentHeight = ClampInt(bottom - top, 0, 32767);

        const int buttonGap = 12;
        const int buttonGroupWidth = ButtonWidth * 2 + buttonGap;
        const int titleWidth = ClampInt(contentWidth - buttonGroupWidth - 18, 160, 320);
        MoveWindow(titleLabel_, left, top - 2, titleWidth, TitleHeight, TRUE);
        MoveWindow(startButton_, right - buttonGroupWidth, top, ButtonWidth, ButtonHeight, TRUE);
        MoveWindow(stopButton_, right - ButtonWidth, top, ButtonWidth, ButtonHeight, TRUE);

        int rowTop = top + TitleHeight + 18;
        const int groupWidth = ClampInt((contentWidth - GroupGap * 2) / 3, 120, 32767);
        const int valueOffset = LabelHeight - 2;

        MoveWindow(communicationCaption_, left, rowTop, groupWidth, LabelHeight, TRUE);
        MoveWindow(communicationValue_, left, rowTop + valueOffset, groupWidth, ValueHeight, TRUE);

        const int backendLeft = left + groupWidth + GroupGap;
        MoveWindow(backendCaption_, backendLeft, rowTop, groupWidth, LabelHeight, TRUE);
        MoveWindow(backendValue_, backendLeft, rowTop + valueOffset, groupWidth, ValueHeight, TRUE);

        const int recorderLeft = backendLeft + groupWidth + GroupGap;
        const int recorderWidth = ClampInt(right - recorderLeft, 120, 32767);
        MoveWindow(recorderCaption_, recorderLeft, rowTop, recorderWidth, LabelHeight, TRUE);
        MoveWindow(recorderValue_, recorderLeft, rowTop + valueOffset, recorderWidth, ValueHeight, TRUE);

        rowTop += valueOffset + ValueHeight + RowGap + 2;
        MoveWindow(outputCaption_, left, rowTop, contentWidth, LabelHeight, TRUE);
        rowTop += LabelHeight + 6;

        const int outputHeight = ClampInt(contentHeight / 5, 56, 96);
        MoveWindow(outputEdit_, left, rowTop, contentWidth, outputHeight, TRUE);

        rowTop += outputHeight + RowGap;
        MoveWindow(messageCaption_, left, rowTop, contentWidth, LabelHeight, TRUE);
        rowTop += LabelHeight + 6;
        MoveWindow(messageEdit_, left, rowTop, contentWidth, ClampInt(bottom - rowTop, 72, 32767), TRUE);

        RECT logRect = pageContentRect_;
        InflateRect(&logRect, -ContentInset, -ContentInset);
        MoveWindow(logEdit_, logRect.left, logRect.top, logRect.right - logRect.left, logRect.bottom - logRect.top, TRUE);

        previewRect_ = pageContentRect_;
        InflateRect(&previewRect_, -ContentInset, -ContentInset);
    }

    void PluginVideoRecordMainWindow::ApplyPageVisibility()
    {
        const bool showRecordPage = GetSelectedPage() == ControllerPage::Record;
        const bool showLogPage = GetSelectedPage() == ControllerPage::Log;

        const HWND recordControls[] =
        {
            titleLabel_,
            communicationCaption_,
            communicationValue_,
            backendCaption_,
            backendValue_,
            recorderCaption_,
            recorderValue_,
            outputCaption_,
            outputEdit_,
            messageCaption_,
            messageEdit_,
            startButton_,
            stopButton_,
        };

        for (HWND controlHandle : recordControls)
        {
            ShowWindow(controlHandle, showRecordPage ? SW_SHOW : SW_HIDE);
        }

        ShowWindow(logEdit_, showLogPage ? SW_SHOW : SW_HIDE);
        InvalidateRect(hwnd_, &pageContentRect_, TRUE);
    }

    ControllerPage PluginVideoRecordMainWindow::GetSelectedPage() const
    {
        if (!tabControl_)
        {
            return ControllerPage::Record;
        }

        int selectedIndex = TabCtrl_GetCurSel(tabControl_);
        if (selectedIndex < 0)
        {
            selectedIndex = 0;
        }

        return static_cast<ControllerPage>(selectedIndex);
    }

    void PluginVideoRecordMainWindow::SetControlText(HWND controlHandle, const std::wstring& text) const
    {
        if (!controlHandle)
        {
            return;
        }

        const int currentLength = GetWindowTextLengthW(controlHandle);
        std::wstring currentText(static_cast<size_t>(currentLength) + 1, L'\0');
        if (currentLength > 0)
        {
            GetWindowTextW(controlHandle, currentText.data(), currentLength + 1);
            currentText.resize(static_cast<size_t>(currentLength));
        }
        else
        {
            currentText.clear();
        }

        if (currentText == text)
        {
            return;
        }

        SetWindowTextW(controlHandle, text.c_str());
    }

    void PluginVideoRecordMainWindow::SetControlFont(HWND controlHandle, HFONT fontHandle) const
    {
        SendMessageW(controlHandle, WM_SETFONT, reinterpret_cast<WPARAM>(fontHandle), TRUE);
    }
}
