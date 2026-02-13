#include "pch.h"
#include "IFGFeature_Vk.h"
#include <State.h>
#include <Config.h>

#include <magic_enum.hpp>

bool IFGFeature_Vk::GetResourceCopy(FG_ResourceType type, VkImageLayout layout, VkImage output)
{
    if (!InitCopyCmdBuffer())
        return false;

    auto resource = GetResource(type);

    if (resource == nullptr || (resource->copyImage == VK_NULL_HANDLE && resource->validity == FG_ResourceValidity::ValidNow))
    {
        LOG_WARN("No resource copy of type {} to use", magic_enum::enum_name(type));
        return false;
    }

    auto fIndex = GetIndex();

    if (!_uiCommandBufferResetted[fIndex])
    {
        // Reset command buffer for this frame
        vkResetCommandBuffer(_copyCommandBuffer[fIndex], 0);
    }

    // Copy image
    VkImageCopy copyRegion {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent = { resource->width, resource->height, 1 };

    vkCmdCopyImage(_copyCommandBuffer[fIndex], resource->GetImage(), resource->imageLayout,
                   output, layout, 1, &copyRegion);

    return true;
}

VkQueue IFGFeature_Vk::GetQueue() { return _gameQueue; }

uint32_t IFGFeature_Vk::GetQueueFamilyIndex() { return _queueFamilyIndex; }

bool IFGFeature_Vk::HasResource(FG_ResourceType type, int index)
{
    std::lock_guard<std::mutex> lock(_frMutex);

    if (index < 0)
        index = GetIndex();

    return _frameResources[index].contains(type);
}

VkCommandBuffer IFGFeature_Vk::GetUICommandBuffer(int index)
{
    if (index < 0)
        index = GetIndex();

    LOG_DEBUG("index: {}", index);

    if (_uiCommandPool[0] == VK_NULL_HANDLE)
    {
        if (_device != VK_NULL_HANDLE)
            CreateObjects(_device);
        else if (State::Instance().currentVkDevice != VK_NULL_HANDLE)
            CreateObjects(State::Instance().currentVkDevice);
        else
            return VK_NULL_HANDLE;
    }

    for (size_t j = 0; j < 2; j++)
    {
        auto i = (index + j) % BUFFER_COUNT;

        if (i != index && _uiCommandBufferResetted[i])
        {
            LOG_DEBUG("Ending _uiCommandBuffer[{}]: {:X}", i, (size_t) _uiCommandBuffer[i]);
            vkEndCommandBuffer(_uiCommandBuffer[i]);
            _uiCommandBufferResetted[i] = false;
        }
    }

    if (!_uiCommandBufferResetted[index])
    {
        VkCommandBufferBeginInfo beginInfo {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        auto result = vkBeginCommandBuffer(_uiCommandBuffer[index], &beginInfo);
        if (result == VK_SUCCESS)
            _uiCommandBufferResetted[index] = true;
        else
            LOG_ERROR("vkBeginCommandBuffer[{}] error: {:X}", index, (UINT) result);
    }

    return _uiCommandBuffer[index];
}

VkResource* IFGFeature_Vk::GetResource(FG_ResourceType type, int index)
{
    std::lock_guard<std::mutex> lock(_frMutex);

    if (index < 0)
        index = GetIndex();

    if (!_frameResources[index].contains(type))
        return nullptr;

    auto& currentIndex = _frameResources[index];
    if (auto it = currentIndex.find(type); it != currentIndex.end())
        return &it->second;

    return nullptr;
}

void IFGFeature_Vk::NewFrame()
{
    if (_waitingNewFrameData)
    {
        LOG_DEBUG("Re-activating FG");
        UpdateTarget();
        Activate();
        _waitingNewFrameData = false;
    }

    auto fIndex = GetIndex();

    std::lock_guard<std::mutex> lock(_frMutex);

    LOG_DEBUG("_frameCount: {}, fIndex: {}", _frameCount, fIndex);

    _frameResources[fIndex].clear();
    _uiCommandBufferResetted[fIndex] = false;
    _lastFGFramePresentId = _fgFramePresentId;
}

void IFGFeature_Vk::FlipResource(VkResource* resource)
{
    // TODO: Implement resource flip for Vulkan if needed
    // This would require Vulkan-specific shader implementations
}

bool IFGFeature_Vk::InitCopyCmdBuffer()
{
    if (_copyCommandPool[0] != VK_NULL_HANDLE)
        return true;

    if (_device == VK_NULL_HANDLE)
        return false;

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_copyCommandPool[i]) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create copy command pool {}", i);
            return false;
        }

        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _copyCommandPool[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(_device, &allocInfo, &_copyCommandBuffer[i]) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to allocate copy command buffer {}", i);
            return false;
        }
    }

    LOG_INFO("Copy command buffers initialized");
    return true;
}

void IFGFeature_Vk::DestroyCopyCmdBuffer()
{
    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_copyCommandBuffer[i] != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(_device, _copyCommandPool[i], 1, &_copyCommandBuffer[i]);
            _copyCommandBuffer[i] = VK_NULL_HANDLE;
        }

        if (_copyCommandPool[i] != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(_device, _copyCommandPool[i], nullptr);
            _copyCommandPool[i] = VK_NULL_HANDLE;
        }
    }
}

bool IFGFeature_Vk::CreateBufferResource(VkDevice device, VkImage source, VkImageLayout layout, VkFormat format,
                                          uint32_t width, uint32_t height, VkImage* outImage, VkImageView* outImageView,
                                          VkDeviceMemory* outMemory, bool UAV, bool depth)
{
    if (device == VK_NULL_HANDLE || source == VK_NULL_HANDLE)
        return false;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (UAV)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (depth)
        format = VK_FORMAT_R32_SFLOAT;

    // Check if we need to recreate
    if (*outImage != VK_NULL_HANDLE)
    {
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = *outImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        // For simplicity, always recreate
        vkDestroyImage(device, *outImage, nullptr);
        vkDestroyImageView(device, *outImageView, nullptr);
        vkFreeMemory(device, *outMemory, nullptr);
        *outImage = VK_NULL_HANDLE;
        *outImageView = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
    }

    VkImageCreateInfo imageInfo {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, outImage) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create buffer image");
        return false;
    }

    // Allocate memory
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, *outImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = 0; // TODO: Find proper memory type

    if (vkAllocateMemory(device, &allocInfo, nullptr, outMemory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate image memory");
        vkDestroyImage(device, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(device, *outImage, *outMemory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, outImageView) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create image view");
        return false;
    }

    return true;
}

void IFGFeature_Vk::ImageMemoryBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                                        VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                        VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmdBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool IFGFeature_Vk::CopyVkImage(VkCommandBuffer cmdBuffer, VkImage source, VkImageLayout sourceLayout,
                                 VkImage* target, VkDeviceMemory* targetMemory, uint32_t width, uint32_t height, VkFormat format)
{
    // TODO: Implement image copy
    return false;
}
