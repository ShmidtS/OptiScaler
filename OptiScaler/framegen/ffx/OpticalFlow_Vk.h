#pragma once

#include <vulkan/vulkan.h>
#include <fsr31/ffx_opticalflow.h>
#include <vk/ffx_api_vk.h>

// Forward declarations
class OpticalFlow_Vk
{
  private:
    VkDevice _device = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkInstance _instance = VK_NULL_HANDLE;

    // Optical Flow context
    ffxContext _opticalFlowContext = nullptr;
    bool _contextCreated = false;

    // Resources
    VkImage _currentFrame = VK_NULL_HANDLE;
    VkImage _previousFrame = VK_NULL_HANDLE;
    VkImage _opticalFlowOutput = VK_NULL_HANDLE;
    VkDeviceMemory _previousFrameMemory = VK_NULL_HANDLE;
    VkDeviceMemory _opticalFlowMemory = VK_NULL_HANDLE;
    VkImageView _previousFrameView = VK_NULL_HANDLE;
    VkImageView _opticalFlowView = VK_NULL_HANDLE;

    // Command buffer for optical flow
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkCommandBuffer _commandBuffer = VK_NULL_HANDLE;

    // Frame info
    uint32_t _width = 0;
    uint32_t _height = 0;
    VkFormat _format = VK_FORMAT_UNDEFINED;
    uint64_t _frameCount = 0;

    bool CreateResources();
    void ReleaseResources();

  public:
    OpticalFlow_Vk() = default;
    ~OpticalFlow_Vk();

    bool CreateContext(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkInstance instance, uint32_t width, uint32_t height);
    void DestroyContext();

    // Compute optical flow from current backbuffer
    bool ComputeOpticalFlow(VkCommandBuffer cmdBuffer, VkImage currentBackbuffer,
                           VkImageLayout currentLayout);

    // Get the computed motion vectors
    VkImage GetMotionVectors() const { return _opticalFlowOutput; }
    VkImageView GetMotionVectorsView() const { return _opticalFlowView; }

    bool IsReady() const { return _contextCreated; }
};
