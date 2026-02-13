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

    // Create FFX Optical Flow context
    ffxCreateContextDescHeader desc {};
    desc.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_OPTICALFLOW;

    // TODO: Add backend description for Vulkan

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
    // TODO: Create images for previous frame storage and optical flow output
    // This requires proper memory allocation and image views
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

    if (!_contextCreated || currentBackbuffer == VK_NULL_HANDLE)
        return false;

    // TODO: Implement optical flow dispatch
    // 1. Copy current backbuffer to previous frame storage
    // 2. Dispatch optical flow compute
    // 3. Swap current/previous for next frame

    _frameCount++;
    _currentFrame = currentBackbuffer;

    return true;
}
