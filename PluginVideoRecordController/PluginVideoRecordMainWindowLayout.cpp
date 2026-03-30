#include "pch.h"

#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordMainWindowInternal.h"

namespace
{
    constexpr int TabMargin = PluginVideoRecord::MainWindowInternal::ScaleUi(10);
    constexpr int ContentInset = PluginVideoRecord::MainWindowInternal::ScaleUi(10);
    constexpr int ButtonWidth = PluginVideoRecord::MainWindowInternal::ScaleUi(76);
    constexpr int ButtonHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(24);
    constexpr int TitleHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(24);
    constexpr int LabelHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(16);
    constexpr int DescriptionHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(14);
    constexpr int HintHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(14);
    constexpr int RowGap = PluginVideoRecord::MainWindowInternal::ScaleUi(6);
    constexpr int ValueHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(20);
    constexpr int MessageHeight = PluginVideoRecord::MainWindowInternal::ScaleUi(42);

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

        const int buttonGap = MainWindowInternal::ScaleUi(8);
        const int buttonGroupWidth = ButtonWidth * 2 + buttonGap;
        const int titleWidth = ClampInt(contentWidth - buttonGroupWidth - MainWindowInternal::ScaleUi(8), 100, 260);
        MoveWindow(titleLabel_, left, top - MainWindowInternal::ScaleUi(2), titleWidth, TitleHeight, TRUE);
        MoveWindow(startButton_, right - buttonGroupWidth, top, ButtonWidth, ButtonHeight, TRUE);
        MoveWindow(stopButton_, right - ButtonWidth, top, ButtonWidth, ButtonHeight, TRUE);

        int rowTop = top + TitleHeight + MainWindowInternal::ScaleUi(8);
        const int columnGap = MainWindowInternal::ScaleUi(10);
        const int columnWidth = ClampInt((contentWidth - columnGap) / 2, 120, 32767);
        const int rightColumnLeft = left + columnWidth + columnGap;
        const int metricBlockHeight = LabelHeight + 2 + ValueHeight;
        const int valueTopOffset = LabelHeight + 2;

        MoveWindow(communicationCaption_, left, rowTop, columnWidth, LabelHeight, TRUE);
        MoveWindow(communicationValue_, left, rowTop + valueTopOffset, columnWidth, ValueHeight, TRUE);
        MoveWindow(backendCaption_, rightColumnLeft, rowTop, columnWidth, LabelHeight, TRUE);
        MoveWindow(backendValue_, rightColumnLeft, rowTop + valueTopOffset, columnWidth, ValueHeight, TRUE);

        rowTop += metricBlockHeight + RowGap;
        MoveWindow(recorderCaption_, left, rowTop, columnWidth, LabelHeight, TRUE);
        MoveWindow(recorderValue_, left, rowTop + valueTopOffset, columnWidth, ValueHeight, TRUE);

        const int vulkanCheckWidth = MainWindowInternal::ScaleUi(58);
        const int vulkanTextWidth = ClampInt(columnWidth - vulkanCheckWidth - MainWindowInternal::ScaleUi(6), 80, 32767);
        const int vulkanCheckTop = rowTop - 1;
        const int vulkanDescriptionTop = rowTop + LabelHeight + 2;
        const int vulkanHintTop = vulkanDescriptionTop + DescriptionHeight + MainWindowInternal::ScaleUi(2);
        MoveWindow(vulkanCaption_, rightColumnLeft, rowTop, vulkanTextWidth, LabelHeight, TRUE);
        MoveWindow(vulkanDescription_, rightColumnLeft, vulkanDescriptionTop, vulkanTextWidth, DescriptionHeight, TRUE);
        MoveWindow(vulkanHint_, rightColumnLeft, vulkanHintTop, vulkanTextWidth, HintHeight, TRUE);
        MoveWindow(
            vulkanEnabledCheck_,
            rightColumnLeft + columnWidth - vulkanCheckWidth,
            vulkanCheckTop,
            vulkanCheckWidth,
            ValueHeight,
            TRUE);

        const int vulkanGroupHeight = LabelHeight + 2 + DescriptionHeight + MainWindowInternal::ScaleUi(2) + HintHeight;

        rowTop += (vulkanGroupHeight > metricBlockHeight ? vulkanGroupHeight : metricBlockHeight) + RowGap + MainWindowInternal::ScaleUi(2);
        MoveWindow(recordBackendCaption_, left, rowTop, contentWidth, LabelHeight, TRUE);
        rowTop += LabelHeight + 2;

        const int radioHeight = ValueHeight;
        const int radioGap = MainWindowInternal::ScaleUi(8);
        const int radioWidth = ClampInt((contentWidth - radioGap * 2) / 3, 80, 32767);
        MoveWindow(backendDx11Radio_, left, rowTop, radioWidth, radioHeight, TRUE);
        MoveWindow(backendDx12Radio_, left + radioWidth + radioGap, rowTop, radioWidth, radioHeight, TRUE);
        MoveWindow(backendVulkanRadio_, left + (radioWidth + radioGap) * 2, rowTop, radioWidth, radioHeight, TRUE);

        rowTop += radioHeight + RowGap;
        MoveWindow(outputCaption_, left, rowTop, contentWidth, LabelHeight, TRUE);
        rowTop += LabelHeight + 4;

        const int outputHeight = ValueHeight;
        MoveWindow(outputEdit_, left, rowTop, contentWidth, outputHeight, TRUE);

        rowTop += outputHeight + RowGap;
        MoveWindow(messageCaption_, left, rowTop, contentWidth, LabelHeight, TRUE);
        rowTop += LabelHeight + 4;
        MoveWindow(messageEdit_, left, rowTop, contentWidth, ClampInt(MessageHeight, ValueHeight * 2, 32767), TRUE);

        RECT logRect = pageContentRect_;
        InflateRect(&logRect, -ContentInset, -ContentInset);
        MoveWindow(logEdit_, logRect.left, logRect.top, logRect.right - logRect.left, logRect.bottom - logRect.top, TRUE);

        previewRect_ = pageContentRect_;
        InflateRect(&previewRect_, -ContentInset, -ContentInset);
        MoveWindow(
            previewPanel_,
            previewRect_.left,
            previewRect_.top,
            previewRect_.right - previewRect_.left,
            previewRect_.bottom - previewRect_.top,
            TRUE);
        SetWindowPos(previewPanel_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    void PluginVideoRecordMainWindow::ApplyPageVisibility()
    {
        const bool showRecordPage = GetSelectedPage() == ControllerPage::Record;
        const bool showPreviewPage = GetSelectedPage() == ControllerPage::Preview;
        const bool showLogPage = GetSelectedPage() == ControllerPage::Log;

        const HWND recordControls[] =
        {
            titleLabel_,
            communicationCaption_,
            communicationValue_,
            backendCaption_,
            backendValue_,
            recordBackendCaption_,
            backendDx11Radio_,
            backendDx12Radio_,
            backendVulkanRadio_,
            recorderCaption_,
            recorderValue_,
            vulkanCaption_,
            vulkanDescription_,
            vulkanHint_,
            vulkanEnabledCheck_,
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

        ShowWindow(previewPanel_, showPreviewPage ? SW_SHOW : SW_HIDE);
        ShowWindow(logEdit_, showLogPage ? SW_SHOW : SW_HIDE);
        InvalidateRect(hwnd_, &pageContentRect_, TRUE);
        if (showPreviewPage && previewPanel_)
        {
            SetWindowPos(previewPanel_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            RedrawWindow(
                previewPanel_,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME);
        }
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
