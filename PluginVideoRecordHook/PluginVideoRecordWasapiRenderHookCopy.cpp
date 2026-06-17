#include "pch.h"

#include "PluginVideoRecordWasapiRenderHookCopy.h"

namespace PluginVideoRecord::WasapiRenderHookInternal
{
    bool CopyRenderBytesCached(
        const WasapiSourceFormat& format,
        BYTE* source,
        UINT32 frameCount,
        std::vector<std::uint8_t>& destination)
    {
        if (!source)
        {
            return false;
        }

        const size_t byteCount = PluginVideoRecord::GetWasapiRenderBufferByteCount(format, frameCount);
        if (byteCount == 0)
        {
            return false;
        }

        thread_local const void* cachedAddress = nullptr;
        thread_local size_t cachedBytes = 0;
        thread_local bool cachedValid = false;

        destination.resize(byteCount);
        return TryCopyMemoryCached(
            destination.data(),
            source,
            byteCount,
            cachedAddress,
            cachedBytes,
            cachedValid);
    }

    PendingRenderSnapshot TakePendingRenderSnapshot(
        HookRuntime& runtime,
        IAudioRenderClient* renderClient,
        UINT32 frameCount,
        DWORD flags)
    {
        PendingRenderSnapshot snapshot = {};
        std::lock_guard<std::mutex> lock(runtime.mutex);

        auto renderState = runtime.renderClients.find(renderClient);
        if (renderState == runtime.renderClients.end() ||
            !renderState->second.formatReady ||
            !renderState->second.pending)
        {
            return snapshot;
        }

        const UINT32 copyFrameCount = std::min(frameCount, renderState->second.pendingFrameCount);

        snapshot.format = renderState->second.format;
        snapshot.formatSource = renderState->second.formatSource;
        snapshot.buffer = renderState->second.pendingBuffer;
        snapshot.frameCount = copyFrameCount;
        snapshot.flags = flags;
        snapshot.silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        snapshot.valid = copyFrameCount > 0 &&
            (snapshot.silent || snapshot.buffer);

        renderState->second.pending = false;
        renderState->second.pendingBuffer = nullptr;
        renderState->second.pendingFrameCount = 0;
        return snapshot;
    }
}
