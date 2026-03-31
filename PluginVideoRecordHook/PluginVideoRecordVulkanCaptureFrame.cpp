#include "pch.h"

#include "PluginVideoRecordVulkanCapture.h"
#include "PluginVideoRecordInternalLogger.h"

namespace
{
    LONG g_vulkanCaptureSubmitLogCount = 0;
    LONG g_vulkanCaptureCollectLogCount = 0;
    LONG g_vulkanCaptureSlowCollectLogCount = 0;
    LONG g_vulkanCaptureRuntimeDropLogCount = 0;

    std::wstring BuildVulkanError(const wchar_t* text, VkResult result)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (%d)", text, static_cast<int>(result));
        return buffer;
    }
}

namespace PluginVideoRecord
{
    bool PluginVideoRecordVulkanCapture::CaptureFrame(
        const UrhVulkanHookRuntime* runtime,
        LONGLONG sampleTimeHns,
        CapturedFrame& frame,
        std::wstring& error)
    {
        frame = {};

        if (!MatchesRuntime(runtime))
        {
            if (!RebindRuntime(runtime, error))
            {
                if (InterlockedIncrement(&g_vulkanCaptureRuntimeDropLogCount) <= 24)
                {
                    PvrcInternalLogger::Log(
                        "[PVRC][VulkanCapture] Runtime drift ignored, keep recording alive: %ls",
                        error.c_str());
                }

                error.clear();
                return true;
            }
        }

        bool hasFrame = false;
        if (!CollectCompletedFrame(frame, hasFrame, error))
        {
            return false;
        }

        if (!SubmitCapture(runtime, sampleTimeHns, error))
        {
            return false;
        }

        return true;
    }

    bool PluginVideoRecordVulkanCapture::CollectCompletedFrame(
        CapturedFrame& frame,
        bool& hasFrame,
        std::wstring& error)
    {
        hasFrame = false;

        size_t completedIndex = static_cast<size_t>(-1);
        LONGLONG completedSampleTime = 0x7fffffffffffffffLL;
        for (size_t index = 0; index < readbackSlots_.size(); ++index)
        {
            const ReadbackSlot& slot = readbackSlots_[index];
            if (!slot.submitted)
            {
                continue;
            }

            VkResult waitResult = VK_NOT_READY;
            if (!WaitForSlotFence(slot, 0, waitResult))
            {
                error = BuildVulkanError(L"查询 Vulkan 录屏栅栏状态失败。", waitResult);
                return false;
            }

            if (waitResult != VK_SUCCESS)
            {
                continue;
            }

            if (slot.sampleTimeHns < completedSampleTime)
            {
                completedIndex = index;
                completedSampleTime = slot.sampleTimeHns;
            }
        }

        if (completedIndex == static_cast<size_t>(-1))
        {
            return true;
        }

        ReadbackSlot& slot = readbackSlots_[completedIndex];
        if (vkInvalidateMappedMemoryRanges_)
        {
            VkMappedMemoryRange memoryRange = {};
            memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            memoryRange.memory = slot.readbackMemory;
            memoryRange.offset = 0;
            memoryRange.size = readbackBufferSize_;
            vkInvalidateMappedMemoryRanges_(device_, 1, &memoryRange);
        }

        frame.width = captureWidth_;
        frame.height = captureHeight_;
        frame.sampleTimeHns = slot.sampleTimeHns;
        frame.pixels.resize(static_cast<size_t>(captureWidth_) * captureHeight_ * 4u);
        const ULONGLONG convertBeginTick = GetTickCount64();
        ConvertPixels(slot, frame);
        const ULONGLONG convertElapsedMs = GetTickCount64() - convertBeginTick;
        if (InterlockedIncrement(&g_vulkanCaptureCollectLogCount) <= 8)
        {
            PvrcInternalLogger::Log(
                "[PVRC][VulkanCapture] Collected frame slot=%zu sample=%lld size=%ux%u",
                completedIndex,
                static_cast<long long>(frame.sampleTimeHns),
                frame.width,
                frame.height);
        }
        else if (convertElapsedMs >= 50 && InterlockedIncrement(&g_vulkanCaptureSlowCollectLogCount) <= 16)
        {
            PvrcInternalLogger::Log(
                "[PVRC][VulkanCapture] Slow frame collect slot=%zu elapsedMs=%llu size=%ux%u format=%u",
                completedIndex,
                static_cast<unsigned long long>(convertElapsedMs),
                frame.width,
                frame.height,
                static_cast<unsigned int>(format_));
        }
        slot.submitted = false;
        slot.sampleTimeHns = 0;
        hasFrame = true;
        return true;
    }

    bool PluginVideoRecordVulkanCapture::SubmitCapture(
        const UrhVulkanHookRuntime* runtime,
        LONGLONG sampleTimeHns,
        std::wstring& error)
    {
        if (runtime->imageIndex >= swapchainImages_.size())
        {
            error = L"当前 Vulkan 交换链图像索引无效。";
            return false;
        }

        size_t slotIndex = static_cast<size_t>(-1);
        for (size_t offset = 0; offset < readbackSlots_.size(); ++offset)
        {
            const size_t candidateIndex = (nextSubmitSlotIndex_ + offset) % readbackSlots_.size();
            if (!readbackSlots_[candidateIndex].submitted)
            {
                slotIndex = candidateIndex;
                break;
            }
        }

        if (slotIndex == static_cast<size_t>(-1))
        {
            return true;
        }

        ReadbackSlot& slot = readbackSlots_[slotIndex];
        const VkImage image = swapchainImages_[runtime->imageIndex];

        VkResult result = vkResetFences_(device_, 1, &slot.fence);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法重置 Vulkan 录屏栅栏。", result);
            return false;
        }

        result = vkResetCommandBuffer_(slot.commandBuffer, 0);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法重置 Vulkan 录屏命令缓冲。", result);
            return false;
        }

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer_(slot.commandBuffer, &beginInfo);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法开始 Vulkan 录屏命令缓冲。", result);
            return false;
        }

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier_(
            slot.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        VkBufferImageCopy copyRegion = {};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent.width = captureWidth_;
        copyRegion.imageExtent.height = captureHeight_;
        copyRegion.imageExtent.depth = 1;
        vkCmdCopyImageToBuffer_(
            slot.commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            slot.readbackBuffer,
            1,
            &copyRegion);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier_(
            slot.commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        result = vkEndCommandBuffer_(slot.commandBuffer);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法结束 Vulkan 录屏命令缓冲。", result);
            return false;
        }

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &slot.commandBuffer;
        result = vkQueueSubmit_(queue_, 1, &submitInfo, slot.fence);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法提交 Vulkan 录屏拷贝命令。", result);
            return false;
        }

        if (InterlockedIncrement(&g_vulkanCaptureSubmitLogCount) <= 8)
        {
            PvrcInternalLogger::Log(
                "[PVRC][VulkanCapture] Submitted frame slot=%zu imageIndex=%u family=%u sample=%lld",
                slotIndex,
                runtime->imageIndex,
                queueFamilyIndex_,
                static_cast<long long>(sampleTimeHns));
        }

        slot.submitted = true;
        slot.sampleTimeHns = sampleTimeHns;
        nextSubmitSlotIndex_ = (slotIndex + 1) % readbackSlots_.size();
        return true;
    }

    bool PluginVideoRecordVulkanCapture::WaitForSlotFence(
        const ReadbackSlot& slot,
        std::uint64_t timeout,
        VkResult& result) const
    {
        result = vkWaitForFences_(device_, 1, &slot.fence, VK_TRUE, timeout);
        return result == VK_SUCCESS || result == VK_TIMEOUT || result == VK_NOT_READY;
    }

    bool PluginVideoRecordVulkanCapture::MatchesRuntime(const UrhVulkanHookRuntime* runtime) const
    {
        if (!runtime || !runtime->device || !runtime->queue || !runtime->swapchain)
        {
            return false;
        }

        return runtime->device == device_ &&
            runtime->queue == queue_ &&
            runtime->swapchain == reinterpret_cast<void*>(static_cast<UINT_PTR>(swapchain_)) &&
            (runtime->queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED ||
             runtime->queueFamilyIndex == queueFamilyIndex_) &&
            (runtime->swapchainFormat == 0 ||
             runtime->swapchainFormat == format_) &&
            static_cast<UINT>(runtime->width) == sourceWidth_ &&
            static_cast<UINT>(runtime->height) == sourceHeight_;
    }

    UINT PluginVideoRecordVulkanCapture::GetCaptureWidth() const
    {
        return captureWidth_;
    }

    UINT PluginVideoRecordVulkanCapture::GetCaptureHeight() const
    {
        return captureHeight_;
    }

    void PluginVideoRecordVulkanCapture::ConvertPixels(const ReadbackSlot& slot, CapturedFrame& frame) const
    {
        const auto* source = static_cast<const std::uint8_t*>(slot.mappedMemory);
        if (format_ == VK_FORMAT_B8G8R8A8_UNORM || format_ == VK_FORMAT_B8G8R8A8_SRGB)
        {
            const size_t rowBytes = static_cast<size_t>(captureWidth_) * 4u;
            for (UINT y = 0; y < captureHeight_; ++y)
            {
                const auto* sourceRow = source + static_cast<size_t>(y) * rowBytes;
                auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * rowBytes;
                CopyMemory(destinationRow, sourceRow, rowBytes);
            }

            return;
        }

        for (UINT y = 0; y < captureHeight_; ++y)
        {
            const auto* sourceRow = source + static_cast<size_t>(y) * captureWidth_ * bytesPerPixel_;
            auto* destinationRow = frame.pixels.data() + static_cast<size_t>(y) * captureWidth_ * 4u;
            for (UINT x = 0; x < captureWidth_; ++x)
            {
                const size_t pixelOffset = static_cast<size_t>(x) * bytesPerPixel_;
                const size_t destinationOffset = static_cast<size_t>(x) * 4u;
                destinationRow[destinationOffset + 0] = sourceRow[pixelOffset + 2];
                destinationRow[destinationOffset + 1] = sourceRow[pixelOffset + 1];
                destinationRow[destinationOffset + 2] = sourceRow[pixelOffset + 0];
                destinationRow[destinationOffset + 3] = 0xFF;
            }
        }
    }
}
