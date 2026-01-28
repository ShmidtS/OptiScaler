#include "FT_Vk.h"

#include <Logger.h>
#include <Config.h>

FT_Vk::FT_Vk(std::string InName, VkDevice InDevice, VkFormat InFormat) :
    Shader_Vk(InName, InDevice, VK_NULL_HANDLE), _format(InFormat)
{
    LOG_DEBUG("Creating FT_Vk: {}", InName);
}

FT_Vk::FT_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, VkFormat InFormat) :
    Shader_Vk(InName, InDevice, InPhysicalDevice), _format(InFormat)
{
    LOG_DEBUG("Creating FT_Vk: {}", InName);
}

FT_Vk::~FT_Vk()
{
    if (_bufferView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, _bufferView, nullptr);
        _bufferView = VK_NULL_HANDLE;
    }

    if (_buffer != VK_NULL_HANDLE)
    {
        vkDestroyImage(_device, _buffer, nullptr);
        _buffer = VK_NULL_HANDLE;
    }

    if (_bufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _bufferMemory, nullptr);
        _bufferMemory = VK_NULL_HANDLE;
    }
}

bool FT_Vk::IsFormatCompatible(VkFormat InFormat)
{
    return _format == InFormat;
}

bool FT_Vk::CreateBufferResource(VkDevice InDevice, VkImage InSource, VkImageLayout InState)
{
    if (InDevice == VK_NULL_HANDLE || InSource == VK_NULL_HANDLE)
        return false;

    // For format transfer, we need to create a staging buffer/image
    // This is a simplified implementation

    if (_buffer != VK_NULL_HANDLE)
    {
        // Check if existing buffer matches requirements
        // For now, just recreate if needed
        return true;
    }

    // Create a new image for format transfer
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = _format;
    imageInfo.extent = { bufferWidth, bufferHeight, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(_device, &imageInfo, nullptr, &_buffer) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImage failed for format transfer");
        return false;
    }

    // Allocate memory for the image
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(_device, _buffer, &memReqs);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(_physicalDevice, memReqs.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(_device, &allocInfo, nullptr, &_bufferMemory) != VK_SUCCESS)
    {
        LOG_ERROR("vkAllocateMemory failed for format transfer");
        vkDestroyImage(_device, _buffer, nullptr);
        _buffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkBindImageMemory(_device, _buffer, _bufferMemory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("vkBindImageMemory failed for format transfer");
        vkDestroyImage(_device, _buffer, nullptr);
        vkFreeMemory(_device, _bufferMemory, nullptr);
        _buffer = VK_NULL_HANDLE;
        _bufferMemory = VK_NULL_HANDLE;
        return false;
    }

    // Create image view
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _buffer;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = _format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &viewInfo, nullptr, &_bufferView) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImageView failed for format transfer");
        vkDestroyImage(_device, _buffer, nullptr);
        vkFreeMemory(_device, _bufferMemory, nullptr);
        _buffer = VK_NULL_HANDLE;
        _bufferMemory = VK_NULL_HANDLE;
        return false;
    }

    _init = true;
    LOG_DEBUG("Created format transfer buffer: {}x{}", bufferWidth, bufferHeight);

    return true;
}

bool FT_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImage InResource, VkImage OutResource)
{
    if (!_init || InCmdList == VK_NULL_HANDLE || InResource == VK_NULL_HANDLE || OutResource == VK_NULL_HANDLE)
        return false;

    // For format transfer, we use a compute shader to convert formats
    // This is a simplified implementation - in production, you'd need proper shader code

    // Image barrier for input
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = InResource;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(InCmdList,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Note: In a full implementation, you would dispatch a compute shader here
    // to convert the format. For now, we just return true as a placeholder.

    return true;
}
