#include "pch.h"

#include "Streamline_Inputs_Vk.h"

#include <Config.h>
#include <State.h>
#include <inputs/FfxApi_Vk.h>

void Sl_Inputs_Vk::updateDimensions(uint32_t width, uint32_t height)
{
    if (width > 0 && height > 0)
    {
        _width = width;
        _height = height;
    }
}

bool Sl_Inputs_Vk::setConstants(const sl::Constants& constants, uint32_t frameId)
{
    LOG_TRACE("Sl_Inputs_Vk::setConstants frameId: {}", frameId);

    _currentFrameId = frameId;

    // Check for depth inverted flag
    if (constants.depthInverted != 0)
    {
        if (!infiniteDepth)
        {
            LOG_DEBUG("Depth is inverted");
            infiniteDepth = true;
        }
    }

    return true;
}

bool Sl_Inputs_Vk::reportResource(const sl::ResourceTag& tag, VkCommandBuffer cmdBuffer, uint32_t frameId)
{
    if (!Config::Instance()->FGEnabled.value_or_default())
        return false;

    LOG_DEBUG("Sl_Inputs_Vk::reportResource type: {} lifecycle: {} frameId: {}",
              (int)tag.type, (int)tag.lifecycle, frameId);

    if (tag.resource == nullptr || tag.resource->native == nullptr)
    {
        LOG_TRACE("tag.resource or native is null");
        return false;
    }

    // For Vulkan, native is VkImage
    VkImage vkImage = static_cast<VkImage>(tag.resource->native);

    std::lock_guard<std::mutex> lock(_resourceMutex);

    // Handle different buffer types
    switch (tag.type)
    {
    case sl::kBufferTypeDepth:
    case sl::kBufferTypeHiResDepth:
    case sl::kBufferTypeLinearDepth:
        _depthImage = vkImage;
        _depthReady = true;
        updateDimensions(tag.resource->width, tag.resource->height);
        LOG_TRACE("Captured Vulkan depth image: {:X}, {}x{}", (size_t)vkImage, _width, _height);

        // Store for FFX API FG resources (used by DX12 interop FG)
        if (_mvReady)
        {
            FfxApiVk_SetFGResources(_depthImage, _motionVectorImage, _width, _height);
            LOG_DEBUG("Sl_Inputs_Vk: Set FG resources via FfxApiVk: depth={:X}, mv={:X}, {}x{}",
                      (size_t)_depthImage, (size_t)_motionVectorImage, _width, _height);
        }
        break;

    case sl::kBufferTypeMotionVectors:
        _motionVectorImage = vkImage;
        _mvReady = true;
        updateDimensions(tag.resource->width, tag.resource->height);
        LOG_TRACE("Captured Vulkan motion vector image: {:X}, {}x{}", (size_t)vkImage, _width, _height);

        // Store for FFX API FG resources (used by DX12 interop FG)
        if (_depthReady)
        {
            FfxApiVk_SetFGResources(_depthImage, _motionVectorImage, _width, _height);
            LOG_DEBUG("Sl_Inputs_Vk: Set FG resources via FfxApiVk: depth={:X}, mv={:X}, {}x{}",
                      (size_t)_depthImage, (size_t)_motionVectorImage, _width, _height);
        }
        break;

    case sl::kBufferTypeHUDLessColor:
    case sl::kBufferTypeUIColorAndAlpha:
        // These could be captured for hudless support in the future
        LOG_TRACE("Captured Vulkan UI/HUDLess resource: {:X}", (size_t)vkImage);
        break;

    default:
        LOG_TRACE("Unhandled Vulkan resource type: {}", (int)tag.type);
        break;
    }

    _currentFrameId = frameId;
    return true;
}

void Sl_Inputs_Vk::clearResources()
{
    std::lock_guard<std::mutex> lock(_resourceMutex);

    _depthImage = VK_NULL_HANDLE;
    _motionVectorImage = VK_NULL_HANDLE;
    _depthReady = false;
    _mvReady = false;
    _width = 0;
    _height = 0;

    LOG_TRACE("Sl_Inputs_Vk: Resources cleared");
}

void Sl_Inputs_Vk::markPresent(uint64_t frameId)
{
    LOG_TRACE("Sl_Inputs_Vk::markPresent frameId: {}", frameId);

    // Resources are valid only for current frame in some cases
    // For eOnlyValidNow lifecycle, resources should be consumed before next present

    // Clear "valid now" resources after present
    // But keep persistent resources for the next frame
}