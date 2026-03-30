#pragma once

#include <vkh/vkh.h>

#include "PluginVideoRecordMfWriter.h"

namespace PluginVideoRecord
{
    class PluginVideoRecordVulkanCapture
    {
    public:
        PluginVideoRecordVulkanCapture();
        ~PluginVideoRecordVulkanCapture();

        bool Initialize(const VkhHookRuntime* runtime, std::wstring& error);
        void Shutdown();
        bool CaptureFrame(
            const VkhHookRuntime* runtime,
            LONGLONG sampleTimeHns,
            CapturedFrame& frame,
            std::wstring& error);
        bool MatchesRuntime(const VkhHookRuntime* runtime) const;
        UINT GetCaptureWidth() const;
        UINT GetCaptureHeight() const;

    private:
        struct ReadbackSlot
        {
            VkCommandBuffer commandBuffer;
            VkFence fence;
            VkBuffer readbackBuffer;
            VkDeviceMemory readbackMemory;
            void* mappedMemory;
            LONGLONG sampleTimeHns;
            bool submitted;
        };

        bool ResolveFunctions(std::wstring& error);
        bool ResolveQueueFamilyIndex(std::wstring& error);
        void ResolveCaptureFormat(const VkhHookRuntime* runtime);
        bool RebindRuntime(const VkhHookRuntime* runtime, std::wstring& error);
        bool CreateReadbackResources(const VkhHookRuntime* runtime, std::wstring& error);
        bool AllocateReadbackSlot(ReadbackSlot& slot, std::wstring& error);
        void ReleaseReadbackSlot(ReadbackSlot& slot);
        void ReleaseReadbackResources();
        bool CollectCompletedFrame(CapturedFrame& frame, bool& hasFrame, std::wstring& error);
        bool SubmitCapture(
            const VkhHookRuntime* runtime,
            LONGLONG sampleTimeHns,
            std::wstring& error);
        bool WaitForSlotFence(const ReadbackSlot& slot, std::uint64_t timeout, VkResult& result) const;
        void ConvertPixels(const ReadbackSlot& slot, CapturedFrame& frame) const;
        bool IsSupportedFormat(VkFormat format) const;

        HMODULE vulkanModule_;
        PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;
        PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_;
        PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR_;
        PFN_vkCreateBuffer vkCreateBuffer_;
        PFN_vkDestroyBuffer vkDestroyBuffer_;
        PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements_;
        PFN_vkAllocateMemory vkAllocateMemory_;
        PFN_vkFreeMemory vkFreeMemory_;
        PFN_vkBindBufferMemory vkBindBufferMemory_;
        PFN_vkMapMemory vkMapMemory_;
        PFN_vkUnmapMemory vkUnmapMemory_;
        PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges_;
        PFN_vkGetDeviceQueue vkGetDeviceQueue_;
        PFN_vkGetDeviceQueue2 vkGetDeviceQueue2_;
        PFN_vkCreateCommandPool vkCreateCommandPool_;
        PFN_vkDestroyCommandPool vkDestroyCommandPool_;
        PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers_;
        PFN_vkBeginCommandBuffer vkBeginCommandBuffer_;
        PFN_vkEndCommandBuffer vkEndCommandBuffer_;
        PFN_vkResetCommandBuffer vkResetCommandBuffer_;
        PFN_vkCreateFence vkCreateFence_;
        PFN_vkDestroyFence vkDestroyFence_;
        PFN_vkWaitForFences vkWaitForFences_;
        PFN_vkResetFences vkResetFences_;
        PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier_;
        PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer_;
        PFN_vkQueueSubmit vkQueueSubmit_;

        VkInstance instance_;
        VkDevice device_;
        VkQueue queue_;
        VkSwapchainKHR swapchain_;
        UINT queueFamilyIndex_;
        VkFormat format_;
        UINT sourceWidth_;
        UINT sourceHeight_;
        UINT captureWidth_;
        UINT captureHeight_;
        UINT memoryTypeCount_;
        VkMemoryPropertyFlags memoryTypeFlags_[32];
        UINT selectedReadbackMemoryTypeIndex_;
        VkMemoryPropertyFlags selectedReadbackMemoryFlags_;
        UINT bytesPerPixel_;
        VkCommandPool commandPool_;
        VkDeviceSize readbackBufferSize_;
        std::vector<VkImage> swapchainImages_;
        std::vector<ReadbackSlot> readbackSlots_;
        size_t nextSubmitSlotIndex_;
    };
}
