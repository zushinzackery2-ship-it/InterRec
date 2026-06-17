#pragma once

#include "PluginVideoRecordMfWriter.h"
#include "PluginVideoRecordWasapiAudioConverter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordWasapiRenderCapture
    {
    public:
        PluginVideoRecordWasapiRenderCapture();
        ~PluginVideoRecordWasapiRenderCapture();

        static bool InstallHooks(std::wstring& error);
        static void ShutdownHooks();
        static void SubmitRenderBuffer(WasapiRenderBuffer&& renderBuffer);
        static void ReportFailure(const std::wstring& error);

        bool Start(PluginVideoRecordMfWriter* writer, std::wstring& error);
        void Stop();
        bool TryGetLastError(std::wstring& error) const;
    };
}
