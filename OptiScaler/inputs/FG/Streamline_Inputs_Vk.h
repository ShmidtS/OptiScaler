#pragma once

#include <pch.h>
#include <sl.h>
#include <framegen/IFGFeature_Vk.h>
#include <vulkan/vulkan.h>
#include <NGX/NvNGX.h> // For NGXVulkanResourceHandle

class Sl_Inputs_Vk
{
  private:
    bool infiniteDepth = false;
    sl::EngineType engineType = sl::EngineType::eCount;

    std::mutex _frameBoundaryMutex;
    bool _isFrameFinished = true;

    uint32_t _currentFrameId = 0;
    uint32_t _currentIndex = -1;
    uint32_t _lastFrameId = UINT32_MAX;
    uint32_t _frameIdIndex[BUFFER_COUNT] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };

    uint64_t mvsWidth = 0;
    uint32_t mvsHeight = 0;

    VkQueue _gameQueue = VK_NULL_HANDLE;
    uint32_t _gameQueueFamilyIndex = UINT32_MAX;

    void CheckForFrame(IFGFeature_Vk* fg, uint32_t frameId);
    int IndexForFrameId(uint32_t frameId) const;

  public:
    bool setConstants(const sl::Constants& values, uint32_t frameId);
    bool evaluateState(VkDevice device);
    bool reportResource(const sl::ResourceTag& tag, VkCommandBuffer cmdBuffer, uint32_t frameId);
    void reportEngineType(sl::EngineType type) { engineType = type; };
    bool dispatchFG();
    void markPresent(uint64_t frameId);

    void SetCommandQueue(VkQueue queue, uint32_t queueFamilyIndex) { _gameQueue = queue; _gameQueueFamilyIndex = queueFamilyIndex; }
    VkQueue GetGameQueue() { return _gameQueue; }
    uint32_t GetGameQueueFamilyIndex() { return _gameQueueFamilyIndex; }
};
