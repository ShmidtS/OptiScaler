#pragma once

#include "SysUtils.h"
#include <sl.h>
#include <vulkan/vulkan.h>

// Forward declarations
class IFGFeature_Dx12;

class Sl_Inputs_Vk
{
  private:
    bool infiniteDepth = false;
    sl::EngineType engineType = sl::EngineType::eCount;

    std::mutex _resourceMutex;

    uint32_t _currentFrameId = 0;
    uint32_t _lastFrameId = UINT32_MAX;

    // Vulkan resources captured from Streamline
    VkImage _depthImage = VK_NULL_HANDLE;
    VkImage _motionVectorImage = VK_NULL_HANDLE;
    uint32_t _width = 0;
    uint32_t _height = 0;

    // Track resource validity
    bool _depthReady = false;
    bool _mvReady = false;

    void updateDimensions(uint32_t width, uint32_t height);

  public:
    bool setConstants(const sl::Constants& constants, uint32_t frameId);
    bool reportResource(const sl::ResourceTag& tag, VkCommandBuffer cmdBuffer, uint32_t frameId);
    void reportEngineType(sl::EngineType type) { engineType = type; }

    // Check if resources are ready for FG
    bool hasResources() const { return _depthReady && _mvReady && _depthImage != VK_NULL_HANDLE && _motionVectorImage != VK_NULL_HANDLE; }

    // Get captured resources for FG
    VkImage getDepthImage() const { return _depthImage; }
    VkImage getMotionVectorImage() const { return _motionVectorImage; }
    uint32_t getWidth() const { return _width; }
    uint32_t getHeight() const { return _height; }

    // Clear resources (called on present or reset)
    void clearResources();

    // Mark frame as presented
    void markPresent(uint64_t frameId);
};
