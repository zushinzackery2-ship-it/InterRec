#include "pch.h"

#include "PluginVideoRecordWasapiRenderCapture.h"
#include "PluginVideoRecordWasapiRenderCaptureInternal.h"

namespace PluginVideoRecord
{
    PluginVideoRecordWasapiRenderCapture::PluginVideoRecordWasapiRenderCapture()
    {
    }

    PluginVideoRecordWasapiRenderCapture::~PluginVideoRecordWasapiRenderCapture()
    {
        Stop();
    }

    bool PluginVideoRecordWasapiRenderCapture::InstallHooks(std::wstring& error)
    {
        return WasapiRenderCaptureInternal::InstallHooks(error);
    }

    void PluginVideoRecordWasapiRenderCapture::ShutdownHooks()
    {
        WasapiRenderCaptureInternal::ShutdownHooks();
    }

    void PluginVideoRecordWasapiRenderCapture::SubmitRenderBuffer(WasapiRenderBuffer&& renderBuffer)
    {
        WasapiRenderCaptureInternal::SubmitRenderBuffer(std::move(renderBuffer));
    }

    void PluginVideoRecordWasapiRenderCapture::ReportFailure(const std::wstring& error)
    {
        WasapiRenderCaptureInternal::ReportFailure(error);
    }

    bool PluginVideoRecordWasapiRenderCapture::Start(
        PluginVideoRecordMfWriter* writer,
        std::wstring& error)
    {
        return WasapiRenderCaptureInternal::Start(writer, error);
    }

    void PluginVideoRecordWasapiRenderCapture::Stop()
    {
        WasapiRenderCaptureInternal::Stop();
    }

    bool PluginVideoRecordWasapiRenderCapture::TryGetLastError(std::wstring& error) const
    {
        return WasapiRenderCaptureInternal::TryGetLastError(error);
    }
}
