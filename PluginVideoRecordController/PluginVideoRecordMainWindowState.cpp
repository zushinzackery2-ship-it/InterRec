#include "pch.h"

#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordPreviewRenderer.h"
#include "PluginVideoRecordVulkanLayer.h"

namespace
{
    void ClearPreviewSnapshot(PluginVideoRecord::PreviewFrameSnapshot& preview)
    {
        preview.isValid = false;
        preview.width = 0;
        preview.height = 0;
        preview.sampleTimeHns = 0;
        preview.pixels.clear();
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

    std::wstring GetAvailableBackendText(LONG availableBackendMask)
    {
        std::wstring text;

        if (PluginVideoRecord::HasGraphicsBackend(availableBackendMask, PluginVideoRecord::GraphicsBackend::Vulkan))
        {
            text += L"Vulkan";
        }

        if (PluginVideoRecord::HasGraphicsBackend(availableBackendMask, PluginVideoRecord::GraphicsBackend::Dx12))
        {
            if (!text.empty())
            {
                text += L" / ";
            }

            text += L"DX12";
        }

        if (PluginVideoRecord::HasGraphicsBackend(availableBackendMask, PluginVideoRecord::GraphicsBackend::Dx11))
        {
            if (!text.empty())
            {
                text += L" / ";
            }

            text += L"DX11";
        }

        if (text.empty())
        {
            return L"未识别";
        }

        return text;
    }
}

namespace PluginVideoRecord
{
    void PluginVideoRecordMainWindow::RefreshVulkanLayerState()
    {
        isVulkanLayerEnabled_ = PluginVideoRecordVulkanLayer::IsAvailable() &&
            PluginVideoRecordVulkanLayer::IsEnabled();
        isForceAutoHookEnabled_ = !isVulkanLayerEnabled_;
        UpdateVulkanLayerControls();
    }

    void PluginVideoRecordMainWindow::PaintPreview()
    {
        if (!previewPanel_)
        {
            PAINTSTRUCT paintStruct = {};
            BeginPaint(hwnd_, &paintStruct);
            EndPaint(hwnd_, &paintStruct);
            return;
        }

        RECT clientRect = {};
        GetClientRect(previewPanel_, &clientRect);
        PaintControllerPreview(previewPanel_, clientRect, preview_, isOnline_, recorderState_ == RecorderState::Recording);
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
            PreviewFrameSnapshot nextPreview = preview_;
            if (ipc_.ReadPreview(nextPreview))
            {
                preview_ = std::move(nextPreview);
            }
        }
        else
        {
            ClearPreviewSnapshot(preview_);
        }

        if (GetSelectedPage() == ControllerPage::Preview && previewPanel_)
        {
            InvalidateRect(previewPanel_, nullptr, FALSE);
        }
    }

    void PluginVideoRecordMainWindow::UpdateStatusControls(const SharedState& state, bool isOnline)
    {
        isOnline_ = isOnline;
        recorderState_ = static_cast<RecorderState>(state.recorderState);
        availableBackendMask_ = state.availableBackendMask;
        selectedBackend_ = static_cast<GraphicsBackend>(state.selectedBackend);
        activeRecordingBackend_ = static_cast<GraphicsBackend>(state.activeRecordingBackend);

        if (selectedBackend_ == GraphicsBackend::Unknown)
        {
            selectedBackend_ = ChooseDefaultGraphicsBackend(availableBackendMask_);
        }

        SetControlText(communicationValue_, isOnline_ ? L"正常" : L"离线");
        SetControlText(backendValue_, GetAvailableBackendText(availableBackendMask_));
        SetControlText(recorderValue_, GetRecorderText(recorderState_));
        UpdateBackendSelectionControls();

        std::wstring outputText = state.currentOutputPath[0] != L'\0'
            ? state.currentOutputPath
            : L"等待 Hook 端提供输出路径。";
        SetControlText(outputEdit_, outputText);

        std::wstring messageText;
        if (recorderState_ == RecorderState::Error)
        {
            messageText = activeRecordingBackend_ != GraphicsBackend::Unknown
                ? (state.lastErrorMessage[0] != L'\0'
                    ? state.lastErrorMessage
                    : L"录制发生错误，等待手动结束。")
                : (state.lastErrorMessage[0] != L'\0'
                    ? state.lastErrorMessage
                    : L"录制失败。");
        }
        else if (!isOnline_)
        {
            messageText = L"Hook 端未在线。";
        }
        else if (recorderState_ == RecorderState::Recording)
        {
            messageText = L"录制进行中。";
        }
        else if (selectedBackend_ == GraphicsBackend::Unknown)
        {
            messageText = L"等待至少一个可录制后端被识别。";
        }
        else if (!HasGraphicsBackend(availableBackendMask_, selectedBackend_))
        {
            messageText = L"当前所选录制后端暂不可用。";
        }
        else
        {
            messageText = L"等待录制命令。";
        }

        SetControlText(messageEdit_, messageText);

        const bool canStartRecording =
            isOnline_ &&
            recorderState_ == RecorderState::Standby &&
            selectedBackend_ != GraphicsBackend::Unknown &&
            HasGraphicsBackend(availableBackendMask_, selectedBackend_);
        EnableWindow(startButton_, canStartRecording);
        EnableWindow(
            stopButton_,
            isOnline_ &&
            activeRecordingBackend_ != GraphicsBackend::Unknown &&
            recorderState_ != RecorderState::Standby);
    }

    void PluginVideoRecordMainWindow::UpdateBackendSelectionControls()
    {
        const bool dx11Available = HasGraphicsBackend(availableBackendMask_, GraphicsBackend::Dx11);
        const bool dx12Available = HasGraphicsBackend(availableBackendMask_, GraphicsBackend::Dx12);
        const bool vulkanAvailable = HasGraphicsBackend(availableBackendMask_, GraphicsBackend::Vulkan);
        const bool selectionLocked = recorderState_ == RecorderState::Recording;

        EnableWindow(backendDx11Radio_, dx11Available && !selectionLocked);
        EnableWindow(backendDx12Radio_, dx12Available && !selectionLocked);
        EnableWindow(backendVulkanRadio_, vulkanAvailable && !selectionLocked);

        GraphicsBackend checkedBackend = selectedBackend_;
        if (selectionLocked && activeRecordingBackend_ != GraphicsBackend::Unknown)
        {
            checkedBackend = activeRecordingBackend_;
        }

        SendMessageW(
            backendDx11Radio_,
            BM_SETCHECK,
            checkedBackend == GraphicsBackend::Dx11 ? BST_CHECKED : BST_UNCHECKED,
            0);
        SendMessageW(
            backendDx12Radio_,
            BM_SETCHECK,
            checkedBackend == GraphicsBackend::Dx12 ? BST_CHECKED : BST_UNCHECKED,
            0);
        SendMessageW(
            backendVulkanRadio_,
            BM_SETCHECK,
            checkedBackend == GraphicsBackend::Vulkan ? BST_CHECKED : BST_UNCHECKED,
            0);

        if (!selectionLocked && checkedBackend != GraphicsBackend::Unknown)
        {
            ipc_.SetSelectedBackend(checkedBackend);
        }
    }

    void PluginVideoRecordMainWindow::UpdateVulkanLayerControls()
    {
        const bool available = PluginVideoRecordVulkanLayer::IsAvailable();
        SetControlText(vulkanEnabledCheck_, L"启用");
        SetControlText(
            vulkanDescription_,
            available
                ? L"开启后可识别Vulkan后端"
                : L"Layer 组件缺失");
        SetControlText(
            vulkanHint_,
            available
                ? L"目标进程重启后生效"
                : L"缺少 PluginVideoRecordVkLayer.dll");
        SendMessageW(
            vulkanEnabledCheck_,
            BM_SETCHECK,
            isVulkanLayerEnabled_ ? BST_CHECKED : BST_UNCHECKED,
            0);
        EnableWindow(vulkanEnabledCheck_, available && recorderState_ != RecorderState::Recording);
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
