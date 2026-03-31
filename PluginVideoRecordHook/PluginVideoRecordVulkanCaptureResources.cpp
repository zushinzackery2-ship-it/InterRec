#include "pch.h"

#include "PluginVideoRecordVulkanCapture.h"
#include "PluginVideoRecordInternalLogger.h"

namespace
{
    constexpr size_t ReadbackSlotCount = 3;
    constexpr UINT QueueFamilyProbeLimit = 32;
    constexpr UINT QueueIndexProbeLimit = 8;
    constexpr UINT InvalidMemoryTypeIndex = ~0u;

    std::wstring BuildVulkanError(const wchar_t* text, VkResult result)
    {
        wchar_t buffer[256] = {};
        swprintf_s(buffer, L"%ls (%d)", text, static_cast<int>(result));
        return buffer;
    }

    std::wstring BuildVulkanLoadError(const wchar_t* text)
    {
        return text;
    }

    UINT GetBytesPerPixel(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return 4;

        default:
            return 0;
        }
    }

    int ScoreReadbackMemoryType(VkMemoryPropertyFlags flags)
    {
        if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            return -1;
        }

        int score = 1;
        if ((flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0)
        {
            score += 100;
        }

        if ((flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
        {
            score += 10;
        }

        return score;
    }

    bool SafeResolveQueueByFamily(
        PFN_vkGetDeviceQueue getDeviceQueue,
        VkDevice device,
        UINT queueFamilyIndex,
        UINT queueIndex,
        VkQueue& queue)
    {
        queue = nullptr;
        if (!getDeviceQueue || !device)
        {
            return false;
        }

        __try
        {
            getDeviceQueue(device, queueFamilyIndex, queueIndex, &queue);
            return queue != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            queue = nullptr;
            return false;
        }
    }

    bool SafeResolveQueue2ByFamily(
        PFN_vkGetDeviceQueue2 getDeviceQueue2,
        VkDevice device,
        UINT queueFamilyIndex,
        UINT queueIndex,
        VkQueue& queue)
    {
        queue = nullptr;
        if (!getDeviceQueue2 || !device)
        {
            return false;
        }

        VkDeviceQueueInfo2 queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2;
        queueInfo.queueFamilyIndex = queueFamilyIndex;
        queueInfo.queueIndex = queueIndex;

        __try
        {
            getDeviceQueue2(device, &queueInfo, &queue);
            return queue != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            queue = nullptr;
            return false;
        }
    }

}

namespace PluginVideoRecord
{
    PluginVideoRecordVulkanCapture::PluginVideoRecordVulkanCapture()
        : vulkanModule_(nullptr)
        , vkGetInstanceProcAddr_(nullptr)
        , vkGetDeviceProcAddr_(nullptr)
        , vkGetSwapchainImagesKHR_(nullptr)
        , vkCreateBuffer_(nullptr)
        , vkDestroyBuffer_(nullptr)
        , vkGetBufferMemoryRequirements_(nullptr)
        , vkAllocateMemory_(nullptr)
        , vkFreeMemory_(nullptr)
        , vkBindBufferMemory_(nullptr)
        , vkMapMemory_(nullptr)
        , vkUnmapMemory_(nullptr)
        , vkInvalidateMappedMemoryRanges_(nullptr)
        , vkGetDeviceQueue_(nullptr)
        , vkGetDeviceQueue2_(nullptr)
        , vkCreateCommandPool_(nullptr)
        , vkDestroyCommandPool_(nullptr)
        , vkAllocateCommandBuffers_(nullptr)
        , vkBeginCommandBuffer_(nullptr)
        , vkEndCommandBuffer_(nullptr)
        , vkResetCommandBuffer_(nullptr)
        , vkCreateFence_(nullptr)
        , vkDestroyFence_(nullptr)
        , vkWaitForFences_(nullptr)
        , vkResetFences_(nullptr)
        , vkCmdPipelineBarrier_(nullptr)
        , vkCmdCopyImageToBuffer_(nullptr)
        , vkQueueSubmit_(nullptr)
        , instance_(nullptr)
        , device_(nullptr)
        , queue_(nullptr)
        , swapchain_(0)
        , queueFamilyIndex_(0)
        , format_(0)
        , sourceWidth_(0)
        , sourceHeight_(0)
        , captureWidth_(0)
        , captureHeight_(0)
        , memoryTypeCount_(0)
        , selectedReadbackMemoryTypeIndex_(InvalidMemoryTypeIndex)
        , selectedReadbackMemoryFlags_(0)
        , bytesPerPixel_(0)
        , commandPool_(0)
        , readbackBufferSize_(0)
        , nextSubmitSlotIndex_(0)
    {
        ZeroMemory(memoryTypeFlags_, sizeof(memoryTypeFlags_));
    }

    PluginVideoRecordVulkanCapture::~PluginVideoRecordVulkanCapture()
    {
        Shutdown();
    }

    bool PluginVideoRecordVulkanCapture::Initialize(const UrhVulkanHookRuntime* runtime, std::wstring& error)
    {
        PvrcInternalLogger::Log("[PVRC][VulkanCapture] Initialize begin");
        Shutdown();

        if (!runtime || !runtime->device ||
            !runtime->queue || !runtime->swapchain)
        {
            error = L"Vulkan 运行时缺少录制所需对象，建议启用 Vulkan 专项模式后重启游戏。";
            return false;
        }

        instance_ = reinterpret_cast<VkInstance>(runtime->instance);
        device_ = reinterpret_cast<VkDevice>(runtime->device);
        queue_ = reinterpret_cast<VkQueue>(runtime->queue);
        swapchain_ = static_cast<VkSwapchainKHR>(reinterpret_cast<UINT_PTR>(runtime->swapchain));
        queueFamilyIndex_ = runtime->queueFamilyIndex;
        ResolveCaptureFormat(runtime);
        sourceWidth_ = static_cast<UINT>(runtime->width);
        sourceHeight_ = static_cast<UINT>(runtime->height);
        captureWidth_ = static_cast<UINT>(runtime->width) & ~1u;
        captureHeight_ = static_cast<UINT>(runtime->height) & ~1u;
        memoryTypeCount_ = runtime->memoryTypeCount < 32u ? runtime->memoryTypeCount : 32u;
        ZeroMemory(memoryTypeFlags_, sizeof(memoryTypeFlags_));
        for (UINT index = 0; index < memoryTypeCount_; ++index)
        {
            memoryTypeFlags_[index] = runtime->memoryTypeFlags[index];
        }
        selectedReadbackMemoryTypeIndex_ = InvalidMemoryTypeIndex;
        selectedReadbackMemoryFlags_ = 0;
        bytesPerPixel_ = GetBytesPerPixel(format_);
        nextSubmitSlotIndex_ = 0;

        if (captureWidth_ == 0 || captureHeight_ == 0 || bytesPerPixel_ == 0)
        {
            error = L"Vulkan 录屏尺寸或像素格式无效。";
            return false;
        }

        if (!ResolveFunctions(error))
        {
            PvrcInternalLogger::Log("[PVRC][VulkanCapture] ResolveFunctions failed");
            Shutdown();
            return false;
        }

        PvrcInternalLogger::Log("[PVRC][VulkanCapture] ResolveFunctions success");
        if (!ResolveQueueFamilyIndex(error))
        {
            PvrcInternalLogger::Log("[PVRC][VulkanCapture] ResolveQueueFamilyIndex failed");
            Shutdown();
            return false;
        }

        if (!CreateReadbackResources(runtime, error))
        {
            PvrcInternalLogger::Log("[PVRC][VulkanCapture] CreateReadbackResources failed");
            Shutdown();
            return false;
        }

        PvrcInternalLogger::Log("[PVRC][VulkanCapture] Initialize success");
        return true;
    }

    void PluginVideoRecordVulkanCapture::ResolveCaptureFormat(const UrhVulkanHookRuntime* runtime)
    {
        format_ = static_cast<VkFormat>(runtime ? runtime->swapchainFormat : 0);
        if (IsSupportedFormat(format_))
        {
            return;
        }

        format_ = VK_FORMAT_B8G8R8A8_UNORM;
        PvrcInternalLogger::Log(
            "[PVRC][VulkanCapture] Swapchain format unavailable, fallback to VK_FORMAT_B8G8R8A8_UNORM");
    }

    void PluginVideoRecordVulkanCapture::Shutdown()
    {
        ReleaseReadbackResources();

        swapchainImages_.clear();
        vulkanModule_ = nullptr;
        vkGetInstanceProcAddr_ = nullptr;
        vkGetDeviceProcAddr_ = nullptr;
        vkGetSwapchainImagesKHR_ = nullptr;
        vkCreateBuffer_ = nullptr;
        vkDestroyBuffer_ = nullptr;
        vkGetBufferMemoryRequirements_ = nullptr;
        vkAllocateMemory_ = nullptr;
        vkFreeMemory_ = nullptr;
        vkBindBufferMemory_ = nullptr;
        vkMapMemory_ = nullptr;
        vkUnmapMemory_ = nullptr;
        vkInvalidateMappedMemoryRanges_ = nullptr;
        vkGetDeviceQueue_ = nullptr;
        vkGetDeviceQueue2_ = nullptr;
        vkCreateCommandPool_ = nullptr;
        vkDestroyCommandPool_ = nullptr;
        vkAllocateCommandBuffers_ = nullptr;
        vkBeginCommandBuffer_ = nullptr;
        vkEndCommandBuffer_ = nullptr;
        vkResetCommandBuffer_ = nullptr;
        vkCreateFence_ = nullptr;
        vkDestroyFence_ = nullptr;
        vkWaitForFences_ = nullptr;
        vkResetFences_ = nullptr;
        vkCmdPipelineBarrier_ = nullptr;
        vkCmdCopyImageToBuffer_ = nullptr;
        vkQueueSubmit_ = nullptr;
        instance_ = nullptr;
        device_ = nullptr;
        queue_ = nullptr;
        swapchain_ = 0;
        queueFamilyIndex_ = 0;
        format_ = 0;
        sourceWidth_ = 0;
        sourceHeight_ = 0;
        captureWidth_ = 0;
        captureHeight_ = 0;
        memoryTypeCount_ = 0;
        ZeroMemory(memoryTypeFlags_, sizeof(memoryTypeFlags_));
        selectedReadbackMemoryTypeIndex_ = InvalidMemoryTypeIndex;
        selectedReadbackMemoryFlags_ = 0;
        bytesPerPixel_ = 0;
        commandPool_ = 0;
        readbackBufferSize_ = 0;
        nextSubmitSlotIndex_ = 0;
    }

    bool PluginVideoRecordVulkanCapture::ResolveFunctions(std::wstring& error)
    {
        vulkanModule_ = GetModuleHandleW(L"vulkan-1.dll");
        if (!vulkanModule_)
        {
            error = BuildVulkanLoadError(L"找不到 vulkan-1.dll。");
            return false;
        }

        vkGetInstanceProcAddr_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkanModule_, "vkGetInstanceProcAddr"));
        vkGetDeviceProcAddr_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(vulkanModule_, "vkGetDeviceProcAddr"));
        if (!vkGetInstanceProcAddr_ || !vkGetDeviceProcAddr_)
        {
            error = BuildVulkanLoadError(L"无法解析 Vulkan ProcAddr 导出。");
            return false;
        }

        vkGetSwapchainImagesKHR_ = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetDeviceProcAddr_(device_, "vkGetSwapchainImagesKHR"));
        vkCreateBuffer_ = reinterpret_cast<PFN_vkCreateBuffer>(vkGetDeviceProcAddr_(device_, "vkCreateBuffer"));
        vkDestroyBuffer_ = reinterpret_cast<PFN_vkDestroyBuffer>(vkGetDeviceProcAddr_(device_, "vkDestroyBuffer"));
        vkGetBufferMemoryRequirements_ = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(vkGetDeviceProcAddr_(device_, "vkGetBufferMemoryRequirements"));
        vkAllocateMemory_ = reinterpret_cast<PFN_vkAllocateMemory>(vkGetDeviceProcAddr_(device_, "vkAllocateMemory"));
        vkFreeMemory_ = reinterpret_cast<PFN_vkFreeMemory>(vkGetDeviceProcAddr_(device_, "vkFreeMemory"));
        vkBindBufferMemory_ = reinterpret_cast<PFN_vkBindBufferMemory>(vkGetDeviceProcAddr_(device_, "vkBindBufferMemory"));
        vkMapMemory_ = reinterpret_cast<PFN_vkMapMemory>(vkGetDeviceProcAddr_(device_, "vkMapMemory"));
        vkUnmapMemory_ = reinterpret_cast<PFN_vkUnmapMemory>(vkGetDeviceProcAddr_(device_, "vkUnmapMemory"));
        vkInvalidateMappedMemoryRanges_ = reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(
            vkGetDeviceProcAddr_(device_, "vkInvalidateMappedMemoryRanges"));
        vkGetDeviceQueue_ = reinterpret_cast<PFN_vkGetDeviceQueue>(
            vkGetDeviceProcAddr_(device_, "vkGetDeviceQueue"));
        vkGetDeviceQueue2_ = reinterpret_cast<PFN_vkGetDeviceQueue2>(
            vkGetDeviceProcAddr_(device_, "vkGetDeviceQueue2"));
        vkCreateCommandPool_ = reinterpret_cast<PFN_vkCreateCommandPool>(vkGetDeviceProcAddr_(device_, "vkCreateCommandPool"));
        vkDestroyCommandPool_ = reinterpret_cast<PFN_vkDestroyCommandPool>(vkGetDeviceProcAddr_(device_, "vkDestroyCommandPool"));
        vkAllocateCommandBuffers_ = reinterpret_cast<PFN_vkAllocateCommandBuffers>(vkGetDeviceProcAddr_(device_, "vkAllocateCommandBuffers"));
        vkBeginCommandBuffer_ = reinterpret_cast<PFN_vkBeginCommandBuffer>(vkGetDeviceProcAddr_(device_, "vkBeginCommandBuffer"));
        vkEndCommandBuffer_ = reinterpret_cast<PFN_vkEndCommandBuffer>(vkGetDeviceProcAddr_(device_, "vkEndCommandBuffer"));
        vkResetCommandBuffer_ = reinterpret_cast<PFN_vkResetCommandBuffer>(vkGetDeviceProcAddr_(device_, "vkResetCommandBuffer"));
        vkCreateFence_ = reinterpret_cast<PFN_vkCreateFence>(vkGetDeviceProcAddr_(device_, "vkCreateFence"));
        vkDestroyFence_ = reinterpret_cast<PFN_vkDestroyFence>(vkGetDeviceProcAddr_(device_, "vkDestroyFence"));
        vkWaitForFences_ = reinterpret_cast<PFN_vkWaitForFences>(vkGetDeviceProcAddr_(device_, "vkWaitForFences"));
        vkResetFences_ = reinterpret_cast<PFN_vkResetFences>(vkGetDeviceProcAddr_(device_, "vkResetFences"));
        vkCmdPipelineBarrier_ = reinterpret_cast<PFN_vkCmdPipelineBarrier>(vkGetDeviceProcAddr_(device_, "vkCmdPipelineBarrier"));
        vkCmdCopyImageToBuffer_ = reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(vkGetDeviceProcAddr_(device_, "vkCmdCopyImageToBuffer"));
        vkQueueSubmit_ = reinterpret_cast<PFN_vkQueueSubmit>(vkGetDeviceProcAddr_(device_, "vkQueueSubmit"));

        if (!vkGetSwapchainImagesKHR_ || !vkCreateBuffer_ ||
            !vkDestroyBuffer_ || !vkGetBufferMemoryRequirements_ || !vkAllocateMemory_ || !vkFreeMemory_ ||
            !vkBindBufferMemory_ || !vkMapMemory_ || !vkUnmapMemory_ || !vkCreateCommandPool_ ||
            !vkDestroyCommandPool_ || !vkAllocateCommandBuffers_ || !vkBeginCommandBuffer_ ||
            !vkEndCommandBuffer_ || !vkResetCommandBuffer_ || !vkCreateFence_ || !vkDestroyFence_ ||
            !vkWaitForFences_ || !vkResetFences_ || !vkInvalidateMappedMemoryRanges_ ||
            !vkCmdPipelineBarrier_ || !vkCmdCopyImageToBuffer_ ||
            !vkQueueSubmit_)
        {
            error = BuildVulkanLoadError(L"Vulkan 录屏缺少必要命令。");
            return false;
        }

        return true;
    }

    bool PluginVideoRecordVulkanCapture::RebindRuntime(
        const UrhVulkanHookRuntime* runtime,
        std::wstring& error)
    {
        if (!runtime || !runtime->device || !runtime->queue || !runtime->swapchain)
        {
            error = L"Vulkan 运行时对象不完整，无法重绑录制上下文。";
            return false;
        }

        const UINT nextCaptureWidth = static_cast<UINT>(runtime->width) & ~1u;
        const UINT nextCaptureHeight = static_cast<UINT>(runtime->height) & ~1u;
        if (nextCaptureWidth != captureWidth_ || nextCaptureHeight != captureHeight_)
        {
            error = L"检测到 Vulkan 录制尺寸变化，当前帧已跳过。";
            return false;
        }

        if (runtime->swapchainFormat != 0 &&
            (!IsSupportedFormat(static_cast<VkFormat>(runtime->swapchainFormat)) ||
             static_cast<VkFormat>(runtime->swapchainFormat) != format_))
        {
            error = L"检测到 Vulkan 录制格式变化，当前帧已跳过。";
            return false;
        }

        if (reinterpret_cast<VkDevice>(runtime->device) != device_)
        {
            error = L"检测到 Vulkan 设备重建，当前帧已跳过。";
            return false;
        }

        PvrcInternalLogger::Log(
            "[PVRC][VulkanCapture] Runtime changed, attempting safe live rebind: queue=%p swapchain=%p family=%u format=%u size=%.0fx%.0f",
            runtime->queue,
            runtime->swapchain,
            runtime->queueFamilyIndex,
            runtime->swapchainFormat,
            runtime->width,
            runtime->height);

        ReleaseReadbackResources();
        swapchainImages_.clear();

        queue_ = reinterpret_cast<VkQueue>(runtime->queue);
        swapchain_ = static_cast<VkSwapchainKHR>(reinterpret_cast<UINT_PTR>(runtime->swapchain));
        queueFamilyIndex_ = runtime->queueFamilyIndex;
        sourceWidth_ = static_cast<UINT>(runtime->width);
        sourceHeight_ = static_cast<UINT>(runtime->height);
        captureWidth_ = nextCaptureWidth;
        captureHeight_ = nextCaptureHeight;
        memoryTypeCount_ = runtime->memoryTypeCount < 32u ? runtime->memoryTypeCount : 32u;
        ZeroMemory(memoryTypeFlags_, sizeof(memoryTypeFlags_));
        for (UINT index = 0; index < memoryTypeCount_; ++index)
        {
            memoryTypeFlags_[index] = runtime->memoryTypeFlags[index];
        }
        selectedReadbackMemoryTypeIndex_ = InvalidMemoryTypeIndex;
        selectedReadbackMemoryFlags_ = 0;
        nextSubmitSlotIndex_ = 0;

        if (!ResolveQueueFamilyIndex(error))
        {
            return false;
        }

        if (!CreateReadbackResources(runtime, error))
        {
            return false;
        }

        PvrcInternalLogger::Log(
            "[PVRC][VulkanCapture] Safe live rebind success: queue=%p swapchain=%p family=%u",
            queue_,
            reinterpret_cast<void*>(static_cast<UINT_PTR>(swapchain_)),
            queueFamilyIndex_);
        return true;
    }

    bool PluginVideoRecordVulkanCapture::ResolveQueueFamilyIndex(std::wstring& error)
    {
        if (queueFamilyIndex_ != VK_QUEUE_FAMILY_IGNORED)
        {
            return true;
        }

        for (UINT familyIndex = 0; familyIndex < QueueFamilyProbeLimit; ++familyIndex)
        {
            for (UINT queueIndex = 0; queueIndex < QueueIndexProbeLimit; ++queueIndex)
            {
                VkQueue resolvedQueue = nullptr;
                if (SafeResolveQueueByFamily(
                        vkGetDeviceQueue_,
                        device_,
                        familyIndex,
                        queueIndex,
                        resolvedQueue) &&
                    resolvedQueue == queue_)
                {
                    queueFamilyIndex_ = familyIndex;
                    PvrcInternalLogger::Log(
                        "[PVRC][VulkanCapture] Resolved queue family via vkGetDeviceQueue: family=%u index=%u",
                        familyIndex,
                        queueIndex);
                    return true;
                }

                resolvedQueue = nullptr;
                if (SafeResolveQueue2ByFamily(
                        vkGetDeviceQueue2_,
                        device_,
                        familyIndex,
                        queueIndex,
                        resolvedQueue) &&
                    resolvedQueue == queue_)
                {
                    queueFamilyIndex_ = familyIndex;
                    PvrcInternalLogger::Log(
                        "[PVRC][VulkanCapture] Resolved queue family via vkGetDeviceQueue2: family=%u index=%u",
                        familyIndex,
                        queueIndex);
                    return true;
                }
            }
        }

        for (UINT familyIndex = 0; familyIndex < QueueFamilyProbeLimit; ++familyIndex)
        {
            VkCommandPool commandPool = 0;
            VkCommandPoolCreateInfo poolInfo = {};
            poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = familyIndex;
            const VkResult result = vkCreateCommandPool_(device_, &poolInfo, nullptr, &commandPool);
            if (result != VK_SUCCESS)
            {
                continue;
            }

            if (commandPool && vkDestroyCommandPool_)
            {
                vkDestroyCommandPool_(device_, commandPool, nullptr);
            }

            queueFamilyIndex_ = familyIndex;
            PvrcInternalLogger::Log(
                "[PVRC][VulkanCapture] Fallback queue family guess=%u",
                queueFamilyIndex_);
            return true;
        }

        error = L"无法为当前 Vulkan 队列推断命令池队列族。";
        return false;
    }

    bool PluginVideoRecordVulkanCapture::CreateReadbackResources(
        const UrhVulkanHookRuntime*,
        std::wstring& error)
    {
        PvrcInternalLogger::Log("[PVRC][VulkanCapture] CreateReadbackResources begin");

        std::uint32_t imageCount = 0;
        VkResult result = vkGetSwapchainImagesKHR_(device_, swapchain_, &imageCount, nullptr);
        if (result != VK_SUCCESS || imageCount == 0)
        {
            error = BuildVulkanError(L"无法获取 Vulkan 交换链图像数量。", result);
            return false;
        }

        swapchainImages_.resize(imageCount);
        PvrcInternalLogger::Log("[PVRC][VulkanCapture] Swapchain images count=%u", imageCount);
        result = vkGetSwapchainImagesKHR_(device_, swapchain_, &imageCount, swapchainImages_.data());
        if (result != VK_SUCCESS || imageCount == 0)
        {
            error = BuildVulkanError(L"无法获取 Vulkan 交换链图像。", result);
            return false;
        }

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex_;
        PvrcInternalLogger::Log("[PVRC][VulkanCapture] Creating command pool family=%u", queueFamilyIndex_);
        result = vkCreateCommandPool_(device_, &poolInfo, nullptr, &commandPool_);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法创建 Vulkan 录屏命令池。", result);
            return false;
        }

        readbackBufferSize_ = static_cast<VkDeviceSize>(captureWidth_) * captureHeight_ * bytesPerPixel_;
        readbackSlots_.clear();
        readbackSlots_.resize(ReadbackSlotCount);
        for (ReadbackSlot& slot : readbackSlots_)
        {
            if (!AllocateReadbackSlot(slot, error))
            {
                return false;
            }
        }

        PvrcInternalLogger::Log("[PVRC][VulkanCapture] CreateReadbackResources success");
        return true;
    }

    bool PluginVideoRecordVulkanCapture::AllocateReadbackSlot(ReadbackSlot& slot, std::wstring& error)
    {
        slot = {};

        VkCommandBufferAllocateInfo commandBufferInfo = {};
        commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferInfo.commandPool = commandPool_;
        commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferInfo.commandBufferCount = 1;
        VkResult result = vkAllocateCommandBuffers_(device_, &commandBufferInfo, &slot.commandBuffer);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法分配 Vulkan 录屏命令缓冲。", result);
            return false;
        }

        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        result = vkCreateFence_(device_, &fenceInfo, nullptr, &slot.fence);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法创建 Vulkan 录屏栅栏。", result);
            return false;
        }

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = readbackBufferSize_;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        result = vkCreateBuffer_(device_, &bufferInfo, nullptr, &slot.readbackBuffer);
        if (result != VK_SUCCESS)
        {
            error = BuildVulkanError(L"无法创建 Vulkan 读回缓冲。", result);
            return false;
        }

        VkMemoryRequirements requirements = {};
        vkGetBufferMemoryRequirements_(device_, slot.readbackBuffer, &requirements);

        bool memoryReady = false;
        std::vector<UINT> candidateMemoryTypeIndices;
        candidateMemoryTypeIndices.reserve(32);
        for (UINT memoryTypeIndex = 0; memoryTypeIndex < 32u; ++memoryTypeIndex)
        {
            if ((requirements.memoryTypeBits & (1u << memoryTypeIndex)) == 0)
            {
                continue;
            }

            candidateMemoryTypeIndices.push_back(memoryTypeIndex);
        }

        std::sort(
            candidateMemoryTypeIndices.begin(),
            candidateMemoryTypeIndices.end(),
            [this](UINT lhs, UINT rhs)
            {
                const VkMemoryPropertyFlags lhsFlags = lhs < memoryTypeCount_ ? memoryTypeFlags_[lhs] : 0;
                const VkMemoryPropertyFlags rhsFlags = rhs < memoryTypeCount_ ? memoryTypeFlags_[rhs] : 0;
                return ScoreReadbackMemoryType(lhsFlags) > ScoreReadbackMemoryType(rhsFlags);
            });

        UINT chosenMemoryTypeIndex = InvalidMemoryTypeIndex;
        VkMemoryPropertyFlags chosenMemoryFlags = 0;
        for (UINT memoryTypeIndex : candidateMemoryTypeIndices)
        {
            VkMemoryAllocateInfo allocateInfo = {};
            allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocateInfo.allocationSize = requirements.size;
            allocateInfo.memoryTypeIndex = memoryTypeIndex;
            result = vkAllocateMemory_(device_, &allocateInfo, nullptr, &slot.readbackMemory);
            if (result != VK_SUCCESS || !slot.readbackMemory)
            {
                slot.readbackMemory = 0;
                continue;
            }

            result = vkBindBufferMemory_(device_, slot.readbackBuffer, slot.readbackMemory, 0);
            if (result != VK_SUCCESS)
            {
                vkFreeMemory_(device_, slot.readbackMemory, nullptr);
                slot.readbackMemory = 0;
                continue;
            }

            result = vkMapMemory_(device_, slot.readbackMemory, 0, readbackBufferSize_, 0, &slot.mappedMemory);
            if (result == VK_SUCCESS && slot.mappedMemory)
            {
                memoryReady = true;
                chosenMemoryTypeIndex = memoryTypeIndex;
                chosenMemoryFlags = memoryTypeIndex < memoryTypeCount_ ? memoryTypeFlags_[memoryTypeIndex] : 0;
                break;
            }

            slot.mappedMemory = nullptr;
            vkFreeMemory_(device_, slot.readbackMemory, nullptr);
            slot.readbackMemory = 0;
        }

        if (!memoryReady)
        {
            error = L"找不到可映射的 Vulkan 读回内存类型。";
            return false;
        }

        if (selectedReadbackMemoryTypeIndex_ == InvalidMemoryTypeIndex)
        {
            selectedReadbackMemoryTypeIndex_ = chosenMemoryTypeIndex;
            selectedReadbackMemoryFlags_ = chosenMemoryFlags;
            PvrcInternalLogger::Log(
                "[PVRC][VulkanCapture] Readback memory type=%u flags=0x%08X cached=%u coherent=%u visible=%u",
                selectedReadbackMemoryTypeIndex_,
                static_cast<unsigned int>(selectedReadbackMemoryFlags_),
                (selectedReadbackMemoryFlags_ & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0 ? 1 : 0,
                (selectedReadbackMemoryFlags_ & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0 ? 1 : 0,
                (selectedReadbackMemoryFlags_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 ? 1 : 0);
        }

        slot.submitted = false;
        slot.sampleTimeHns = 0;
        return true;
    }

    void PluginVideoRecordVulkanCapture::ReleaseReadbackSlot(ReadbackSlot& slot)
    {
        if (device_ && slot.mappedMemory && vkUnmapMemory_)
        {
            vkUnmapMemory_(device_, slot.readbackMemory);
        }

        if (device_ && slot.readbackBuffer && vkDestroyBuffer_)
        {
            vkDestroyBuffer_(device_, slot.readbackBuffer, nullptr);
        }

        if (device_ && slot.readbackMemory && vkFreeMemory_)
        {
            vkFreeMemory_(device_, slot.readbackMemory, nullptr);
        }

        if (device_ && slot.fence && vkDestroyFence_)
        {
            vkDestroyFence_(device_, slot.fence, nullptr);
        }

        slot = {};
    }

    void PluginVideoRecordVulkanCapture::ReleaseReadbackResources()
    {
        if (device_ && !readbackSlots_.empty() && vkWaitForFences_)
        {
            std::vector<VkFence> pendingFences;
            for (const ReadbackSlot& slot : readbackSlots_)
            {
                if (slot.submitted && slot.fence)
                {
                    pendingFences.push_back(slot.fence);
                }
            }

            if (!pendingFences.empty())
            {
                vkWaitForFences_(
                    device_,
                    static_cast<std::uint32_t>(pendingFences.size()),
                    pendingFences.data(),
                    VK_TRUE,
                    500000000ull);
            }
        }

        for (ReadbackSlot& slot : readbackSlots_)
        {
            ReleaseReadbackSlot(slot);
        }

        readbackSlots_.clear();

        if (device_ && commandPool_ && vkDestroyCommandPool_)
        {
            vkDestroyCommandPool_(device_, commandPool_, nullptr);
        }
    }

    bool PluginVideoRecordVulkanCapture::IsSupportedFormat(VkFormat format) const
    {
        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return true;

        default:
            return false;
        }
    }
}
