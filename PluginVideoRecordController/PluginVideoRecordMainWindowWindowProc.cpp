#include "pch.h"

#include "PluginVideoRecordHookMode.h"
#include "PluginVideoRecordMainWindow.h"
#include "PluginVideoRecordMainWindowInternal.h"
#include "PluginVideoRecordVulkanLayer.h"

namespace PluginVideoRecord
{
    LRESULT CALLBACK PluginVideoRecordMainWindow::PreviewPanelProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        auto* self = reinterpret_cast<PluginVideoRecordMainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<PluginVideoRecordMainWindow*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }

        if (!self)
        {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        switch (message)
        {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            self->PaintPreview();
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
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

            if (PluginVideoRecordVulkanLayer::IsAvailable())
            {
                std::wstring error;
                if (!PluginVideoRecordVulkanLayer::EnsureManifestReady(error))
                {
                    self->AppendLogLine(L"[UI] Vulkan Layer 清单自修复失败：" + error);
                }
            }

            SetTimer(hwnd, MainWindowInternal::RefreshTimerId, MainWindowInternal::RefreshIntervalMs, nullptr);
            self->RefreshVulkanLayerState();
            {
                std::wstring error;
                if (!PluginVideoRecord::HookMode::SetForceAutoHookEnabled(!self->isVulkanLayerEnabled_, error) &&
                    !error.empty())
                {
                    self->AppendLogLine(L"[UI] 同步非Layer模式标志失败：" + error);
                }
            }
            self->RefreshStatus();
            self->RefreshLogs();
            return 0;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case MainWindowInternal::StartButtonId:
                self->ipc_.SetSelectedBackend(self->selectedBackend_);
                self->AppendLogLine(
                    self->ipc_.SendCommand(CommandType::StartRecording)
                        ? L"[UI] 已发送开始录制命令。"
                        : L"[UI] 发送开始录制命令失败。");
                self->RefreshStatus();
                return 0;

            case MainWindowInternal::StopButtonId:
                self->AppendLogLine(
                    self->ipc_.SendCommand(CommandType::StopRecording)
                        ? L"[UI] 已发送停止录制命令。"
                        : L"[UI] 发送停止录制命令失败。");
                self->RefreshStatus();
                return 0;

            case MainWindowInternal::VulkanEnhancedCheckId:
            {
                std::wstring error;
                const bool enable = !self->isVulkanLayerEnabled_;
                if (PluginVideoRecordVulkanLayer::SetEnabled(enable, error))
                {
                    std::wstring hookModeError;
                    if (!PluginVideoRecord::HookMode::SetForceAutoHookEnabled(!enable, hookModeError) &&
                        !hookModeError.empty())
                    {
                        self->AppendLogLine(L"[UI] 警告：" + hookModeError);
                    }

                    self->RefreshVulkanLayerState();
                    self->AppendLogLine(
                        enable
                            ? L"[UI] 已启用 Vulkan-Layer模式，重启 Vulkan 游戏后生效。"
                            : L"[UI] 已关闭 Vulkan-Layer模式，重启 Vulkan 游戏后生效。");
                    if (!error.empty())
                    {
                        self->AppendLogLine(L"[UI] 警告：" + error);
                    }
                }
                else
                {
                    self->RefreshVulkanLayerState();
                    self->AppendLogLine(L"[UI] Vulkan-Layer模式切换失败：" + error);
                }
                return 0;
            }

            case MainWindowInternal::BackendDx11RadioId:
            case MainWindowInternal::BackendDx12RadioId:
            case MainWindowInternal::BackendVulkanRadioId:
                if (self->recorderState_ == RecorderState::Recording)
                {
                    return 0;
                }

                if (LOWORD(wParam) == MainWindowInternal::BackendDx11RadioId)
                {
                    self->selectedBackend_ = GraphicsBackend::Dx11;
                }
                else if (LOWORD(wParam) == MainWindowInternal::BackendDx12RadioId)
                {
                    self->selectedBackend_ = GraphicsBackend::Dx12;
                }
                else
                {
                    self->selectedBackend_ = GraphicsBackend::Vulkan;
                }

                self->ipc_.SetSelectedBackend(self->selectedBackend_);
                self->UpdateBackendSelectionControls();
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
            RedrawWindow(
                hwnd,
                nullptr,
                nullptr,
                RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_ERASE | RDW_FRAME);
            return 0;

        case WM_GETMINMAXINFO:
        {
            auto* minMaxInfo = reinterpret_cast<MINMAXINFO*>(lParam);
            minMaxInfo->ptMinTrackSize.x = MainWindowInternal::MinWindowWidth;
            minMaxInfo->ptMinTrackSize.y = MainWindowInternal::MinWindowHeight;
            return 0;
        }

        case WM_TIMER:
            if (wParam == MainWindowInternal::RefreshTimerId)
            {
                self->RefreshVulkanLayerState();
                self->RefreshStatus();
                self->RefreshLogs();
                return 0;
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd, MainWindowInternal::RefreshTimerId);
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
