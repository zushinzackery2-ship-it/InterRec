#pragma once

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordMainWindow
    {
    public:
        PluginVideoRecordMainWindow();

        int Run(HINSTANCE instanceHandle, int showCommand);

    private:
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool Create(HINSTANCE instanceHandle, int showCommand);
        void CreateControls();
        void RefreshStatus();
        void UpdateLabel(HWND controlHandle, const std::wstring& text) const;

        HWND hwnd_;
        HWND communicationLabel_;
        HWND recorderLabel_;
        HWND startButton_;
        HWND stopButton_;
        PluginVideoRecordControllerIpc ipc_;
    };
}
