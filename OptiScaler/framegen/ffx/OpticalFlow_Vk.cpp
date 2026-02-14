#include "pch.h"
#include "OpticalFlow_Vk.h"

#include <State.h>
#include <Config.h>
#include <proxies/FfxApi_Proxy.h>

OpticalFlow_Vk::~OpticalFlow_Vk()
{
    DestroyContext();
}

bool OpticalFlow_Vk::CreateContext(VkDevice device, VkPhysicalDevice physicalDevice,
                                   VkInstance instance, uint32_t width, uint32_t height)
{
    LOG_FUNC();

    if (_contextCreated)
    {
        LOG_WARN("Optical Flow context already created");
        return true;
    }

    if (!FfxApiProxy::IsFGReady())
    {
        LOG_ERROR("FG API not ready!");
        return false;
    }

    _device = device;
    _physicalDevice = physicalDevice;
    _instance = instance;
    _width = width;
    _height = height;

    // Create FFX Optical Flow context with Vulkan backend
    ffxCreateContextDescHeader desc {};
    desc.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_OPTICALFLOW;

    // Setup Vulkan backend description
    ffxBackendDescriptionDescVK backendDesc {};
    backendDesc.header.type = FFX_API_DESC_TYPE_BACKEND_VK;
    backendDesc.device = device;
    backendDesc.instance = instance;
    backendDesc.physicalDevice = physicalDevice;

    // Chain the descriptions
    desc.pNext = &backendDesc;

    auto result = FfxApiProxy::VULKAN_CreateContext()(&_opticalFlowContext, &desc, nullptr);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to create Optical Flow context: {:X}", (UINT) result);
        return false;
    }

    // Create command pool
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = State::Instance().currentVkQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create command pool for Optical Flow");
        FfxApiProxy::VULKAN_DestroyContext()(&_opticalFlowContext, nullptr);
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(_device, &allocInfo, &_commandBuffer) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate command buffer for Optical Flow");
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        FfxApiProxy::VULKAN_DestroyContext()(&_opticalFlowContext, nullptr);
        return false;
    }

    _contextCreated = true;
    LOG_INFO("Optical Flow Vulkan context created");
    return true;
}

void OpticalFlow_Vk::DestroyContext()
{
    LOG_FUNC();

    if (!_contextCreated)
        return;

    ReleaseResources();

    if (_opticalFlowContext != nullptr)
    {
        FfxApiProxy::VULKAN_DestroyContext()(&_opticalFlowContext, nullptr);
        _opticalFlowContext = nullptr;
    }

    if (_commandBuffer != VK_NULL_HANDLE && _commandPool != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(_device, _commandPool, 1, &_commandBuffer);
        _commandBuffer = VK_NULL_HANDLE;
    }

    if (_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
    }

    _contextCreated = false;
    LOG_INFO("Optical Flow Vulkan context destroyed");
}

bool OpticalFlow_Vk::CreateResources()
{
    if (_device == VK_NULL_HANDLE || _width == 0 || _height == 0)
        return false;

    // Release existing resources
    ReleaseResources();

    // Find memory type index
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

    auto findMemoryType = [&](uint32_t typeBits, VkMemoryPropertyFlags properties) -> uint32_t {
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
                return i;
        }
        return UINT32_MAX;
    };

    // Create previous frame image
    VkImageCreateInfo prevImageInfo {};
    prevImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    prevImageInfo.imageType = VK_IMAGE_TYPE_2D;
    prevImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    prevImageInfo.extent.width = _width;
    prevImageInfo.extent.height = _height;
    prevImageInfo.extent.depth = 1;
    prevImageInfo.mipLevels = 1;
    prevImageInfo.arrayLayers = 1;
    prevImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    prevImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    prevImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    prevImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    prevImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(_device, &prevImageInfo, nullptr, &_previousFrame) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create previous frame image for Optical Flow");
        return false;
    }

    VkMemoryRequirements prevMemReqs;
    vkGetImageMemoryRequirements(_device, _previousFrame, &prevMemReqs);

    uint32_t prevMemType = findMemoryType(prevMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (prevMemType == UINT32_MAX)
    {
        LOG_ERROR("Failed to find memory type for previous frame");
        return false;
    }

    VkMemoryAllocateInfo prevAllocInfo {};
    prevAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    prevAllocInfo.allocationSize = prevMemReqs.size;
    prevAllocInfo.memoryTypeIndex = prevMemType;

    if (vkAllocateMemory(_device, &prevAllocInfo, nullptr, &_previousFrameMemory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate memory for previous frame");
        return false;
    }

    vkBindImageMemory(_device, _previousFrame, _previousFrameMemory, 0);

    // Create previous frame image view
    VkImageViewCreateInfo prevViewInfo {};
    prevViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    prevViewInfo.image = _previousFrame;
    prevViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    prevViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    prevViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    prevViewInfo.subresourceRange.levelCount = 1;
    prevViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &prevViewInfo, nullptr, &_previousFrameView) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create previous frame image view");
        return false;
    }

    // Create optical flow output image (motion vectors - typically R16G16_SFLOAT)
    VkImageCreateInfo ofImageInfo {};
    ofImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ofImageInfo.imageType = VK_IMAGE_TYPE_2D;
    ofImageInfo.format = VK_FORMAT_R16G16_SFLOAT;
    ofImageInfo.extent.width = _width;
    ofImageInfo.extent.height = _height;
    ofImageInfo.extent.depth = 1;
    ofImageInfo.mipLevels = 1;
    ofImageInfo.arrayLayers = 1;
    ofImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    ofImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    ofImageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ofImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ofImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(_device, &ofImageInfo, nullptr, &_opticalFlowOutput) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create optical flow output image");
        return false;
    }

    VkMemoryRequirements ofMemReqs;
    vkGetImageMemoryRequirements(_device, _opticalFlowOutput, &ofMemReqs);

    uint32_t ofMemType = findMemoryType(ofMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ofMemType == UINT32_MAX)
    {
        LOG_ERROR("Failed to find memory type for optical flow output");
        return false;
    }

    VkMemoryAllocateInfo ofAllocInfo {};
    ofAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ofAllocInfo.allocationSize = ofMemReqs.size;
    ofAllocInfo.memoryTypeIndex = ofMemType;

    if (vkAllocateMemory(_device, &ofAllocInfo, nullptr, &_opticalFlowMemory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate memory for optical flow output");
        return false;
    }

    vkBindImageMemory(_device, _opticalFlowOutput, _opticalFlowMemory, 0);

    // Create optical flow output image view
    VkImageViewCreateInfo ofViewInfo {};
    ofViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ofViewInfo.image = _opticalFlowOutput;
    ofViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ofViewInfo.format = VK_FORMAT_R16G16_SFLOAT;
    ofViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ofViewInfo.subresourceRange.levelCount = 1;
    ofViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device, &ofViewInfo, nullptr, &_opticalFlowView) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create optical flow output image view");
        return false;
    }

    LOG_INFO("Optical Flow Vulkan resources created: {}x{}", _width, _height);
    return true;
}

void OpticalFlow_Vk::ReleaseResources()
{
    if (_opticalFlowView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, _opticalFlowView, nullptr);
        _opticalFlowView = VK_NULL_HANDLE;
    }

    if (_previousFrameView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, _previousFrameView, nullptr);
        _previousFrameView = VK_NULL_HANDLE;
    }

    if (_opticalFlowMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _opticalFlowMemory, nullptr);
        _opticalFlowMemory = VK_NULL_HANDLE;
    }

    if (_previousFrameMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(_device, _previousFrameMemory, nullptr);
        _previousFrameMemory = VK_NULL_HANDLE;
    }

    if (_opticalFlowOutput != VK_NULL_HANDLE)
    {
        vkDestroyImage(_device, _opticalFlowOutput, nullptr);
        _opticalFlowOutput = VK_NULL_HANDLE;
    }

    if (_previousFrame != VK_NULL_HANDLE)
    {
        vkDestroyImage(_device, _previousFrame, nullptr);
        _previousFrame = VK_NULL_HANDLE;
    }
}

bool OpticalFlow_Vk::ComputeOpticalFlow(VkCommandBuffer cmdBuffer, VkImage currentBackbuffer,
                                        VkImageLayout currentLayout)
{
    LOG_FUNC();

    if (!_contextCreated || currentBackbuffer == VK_NULL_HANDLE || _opticalFlowContext == nullptr)
        return false;

    // Create resources on first use
    if (_previousFrame == VK_NULL_HANDLE)
    {
        if (!CreateResources())
            return false;
    }

    // On first frame, just store the current frame and return
    if (_frameCount == 0)
    {
        // Copy current backbuffer to previous frame storage
        VkImageSubresourceRange subresourceRange {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.levelCount = 1;
        subresourceRange.layerCount = 1;

        // Transition previous frame to transfer dst
        VkImageMemoryBarrier barrier {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = _previousFrame;
        barrier.subresourceRange = subresourceRange;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy current to previous
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
        copyRegion.extent = { _width, _height, 1 };

        vkCmdCopyImage(cmdBuffer, currentBackbuffer, currentLayout,
                       _previousFrame, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // Transition previous frame to shader read
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        _currentFrame = currentBackbuffer;
        _frameCount++;
        return true; // First frame - no optical flow yet
    }

    // Dispatch optical flow using FFX API
    ffxDispatchDescOpticalFlow ofDesc {};
    ofDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_OPTICALFLOW;
    ofDesc.commandList = cmdBuffer;

    // Set current frame resource
    ofDesc.color.resource = currentBackbuffer;
    ofDesc.color.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    ofDesc.color.description.width = _width;
    ofDesc.color.description.height = _height;
    ofDesc.color.description.format = FFX_API_RESOURCE_FORMAT_R8G8B8A8_UNORM;
    ofDesc.color.description.mipCount = 1;
    ofDesc.color.description.flags = 0;
    ofDesc.color.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    ofDesc.color.state = FFX_API_RESOURCE_STATE_COMPUTE_READ;

    // Set output motion vectors
    ofDesc.output.resource = _opticalFlowOutput;
    ofDesc.output.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    ofDesc.output.description.width = _width;
    ofDesc.output.description.height = _height;
    ofDesc.output.description.format = FFX_API_RESOURCE_FORMAT_R16G16_FLOAT;
    ofDesc.output.description.mipCount = 1;
    ofDesc.output.description.flags = 0;
    ofDesc.output.description.usage = FFX_API_RESOURCE_USAGE_UAV;
    ofDesc.output.state = FFX_API_RESOURCE_STATE_UNORDERED_ACCESS;

    ofDesc.frameID = _frameCount;
    ofDesc.reset = false;

    auto result = FfxApiProxy::VULKAN_Dispatch()(&_opticalFlowContext, (ffxDispatchDescHeader*) &ofDesc);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Optical Flow dispatch failed: {:X}", (UINT) result);
        return false;
    }

    // Copy current frame to previous for next frame
    VkImageSubresourceRange subresourceRange {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _previousFrame;
    barrier.subresourceRange = subresourceRange;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

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
    copyRegion.extent = { _width, _height, 1 };

    vkCmdCopyImage(cmdBuffer, currentBackbuffer, currentLayout,
                   _previousFrame, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Transition back to shader read for next frame
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    _currentFrame = currentBackbuffer;
    _frameCount++;

    return true;
}
