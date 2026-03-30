#pragma once

namespace PluginVideoRecord::MainWindowInternal
{
    constexpr UINT_PTR RefreshTimerId = 1;
    constexpr UINT RefreshIntervalMs = 16;

    constexpr int StartButtonId = 1001;
    constexpr int StopButtonId = 1002;
    constexpr int VulkanEnhancedCheckId = 1003;
    constexpr int BackendDx11RadioId = 1004;
    constexpr int BackendDx12RadioId = 1005;
    constexpr int BackendVulkanRadioId = 1006;

    constexpr int InitialWindowWidth = 440;
    constexpr int InitialWindowHeight = 356;
    constexpr int MinWindowWidth = 410;
    constexpr int MinWindowHeight = 330;

    constexpr wchar_t WindowClassName[] = L"PluginVideoRecordControllerWindow";
    constexpr wchar_t PreviewPanelClassName[] = L"PluginVideoRecordPreviewPanel";

    constexpr int UiScaleNumerator = 4;
    constexpr int UiScaleDenominator = 5;

    constexpr int ScaleUi(int value)
    {
        return (value * UiScaleNumerator + UiScaleDenominator / 2) / UiScaleDenominator;
    }

    constexpr int ScaleFontHeight(int value)
    {
        return value < 0 ? -ScaleUi(-value) : ScaleUi(value);
    }
}
