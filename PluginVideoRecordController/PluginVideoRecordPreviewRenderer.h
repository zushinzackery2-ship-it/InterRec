#pragma once

#include "PluginVideoRecordControllerIpc.h"

namespace PluginVideoRecord
{
    void PaintControllerPreview(
        HWND windowHandle,
        const RECT& previewRect,
        const PreviewFrameSnapshot& preview,
        bool isOnline,
        bool isRecording);
}
