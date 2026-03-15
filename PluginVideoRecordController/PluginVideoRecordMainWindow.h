#pragma once

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    enum class ControllerPage
    {
        Record = 0,
        Preview = 1,
        Log = 2,
    };

    class PluginVideoRecordMainWindow
    {
    public:
        PluginVideoRecordMainWindow();
        ~PluginVideoRecordMainWindow();

        int Run(HINSTANCE instanceHandle, int showCommand);

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK PreviewPanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool Create(HINSTANCE instanceHandle, int showCommand);
        bool CreateFonts();
        void CreateControls();
        void DestroyFonts();
        void LayoutControls();
        void ApplyPageVisibility();
        ControllerPage GetSelectedPage() const;
        void PaintPreview();
        void RefreshStatus();
        void RefreshLogs();
        void UpdatePreview(bool isRecording);
        void UpdateStatusControls(const SharedState& state, bool isOnline);
        void AppendLogLine(const std::wstring& text);
        void SetControlText(HWND controlHandle, const std::wstring& text) const;
        void SetControlFont(HWND controlHandle, HFONT fontHandle) const;

        HWND hwnd_;
        HWND tabControl_;
        HWND titleLabel_;
        HWND communicationCaption_;
        HWND communicationValue_;
        HWND backendCaption_;
        HWND backendValue_;
        HWND recorderCaption_;
        HWND recorderValue_;
        HWND outputCaption_;
        HWND outputEdit_;
        HWND messageCaption_;
        HWND messageEdit_;
        HWND startButton_;
        HWND stopButton_;
        HWND logEdit_;
        HWND previewPanel_;
        RECT pageContentRect_;
        RECT previewRect_;
        HFONT regularFont_;
        HFONT titleFont_;
        HFONT monoFont_;
        bool isOnline_;
        RecorderState recorderState_;
        GraphicsBackend graphicsBackend_;
        PreviewFrameSnapshot preview_;
        PluginVideoRecordControllerIpc ipc_;
    };
}
