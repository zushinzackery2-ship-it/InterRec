#include "pch.h"

#include "PluginVideoRecordPreviewRenderer.h"

namespace
{
    int MaxInt(int left, int right)
    {
        return left > right ? left : right;
    }
}

namespace PluginVideoRecord
{
    void PaintControllerPreview(
        HWND windowHandle,
        const RECT& previewRect,
        const PreviewFrameSnapshot& preview,
        bool isOnline,
        bool isRecording)
    {
        UNREFERENCED_PARAMETER(isOnline);
        UNREFERENCED_PARAMETER(isRecording);

        PAINTSTRUCT paintStruct = {};
        HDC deviceContext = BeginPaint(windowHandle, &paintStruct);

        HBRUSH backgroundBrush = CreateSolidBrush(RGB(24, 24, 24));
        FillRect(deviceContext, &previewRect, backgroundBrush);
        DeleteObject(backgroundBrush);

        RECT innerRect = previewRect;
        InflateRect(&innerRect, -2, -2);

        if (preview.isValid && !preview.pixels.empty())
        {
            const int availableWidth = MaxInt(1, innerRect.right - innerRect.left);
            const int availableHeight = MaxInt(1, innerRect.bottom - innerRect.top);
            int drawWidth = availableWidth;
            int drawHeight = MulDiv(drawWidth, static_cast<int>(preview.height), static_cast<int>(preview.width));
            if (drawHeight > availableHeight)
            {
                drawHeight = availableHeight;
                drawWidth = MulDiv(drawHeight, static_cast<int>(preview.width), static_cast<int>(preview.height));
            }

            RECT drawRect = {};
            drawRect.left = innerRect.left + (availableWidth - drawWidth) / 2;
            drawRect.top = innerRect.top + (availableHeight - drawHeight) / 2;
            drawRect.right = drawRect.left + drawWidth;
            drawRect.bottom = drawRect.top + drawHeight;

            BITMAPINFO bitmapInfo = {};
            bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
            bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(preview.width);
            bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(preview.height);
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            StretchDIBits(
                deviceContext,
                drawRect.left,
                drawRect.top,
                drawRect.right - drawRect.left,
                drawRect.bottom - drawRect.top,
                0,
                0,
                static_cast<int>(preview.width),
                static_cast<int>(preview.height),
                preview.pixels.data(),
                &bitmapInfo,
                DIB_RGB_COLORS,
                SRCCOPY);
        }

        EndPaint(windowHandle, &paintStruct);
    }
}
