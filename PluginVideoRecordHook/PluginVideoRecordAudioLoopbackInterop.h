#pragma once

namespace PluginVideoRecord
{
    HRESULT ActivateProcessLoopbackAudioClient(
        DWORD processId,
        Microsoft::WRL::ComPtr<IAudioClient>& audioClient);

    WAVEFORMATEX BuildLoopbackCaptureFormat();
}
