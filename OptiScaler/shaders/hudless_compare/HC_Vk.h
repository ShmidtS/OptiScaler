#pragma once

#include <pch.h>

#include <vulkan/vulkan.h>
#include "../Shader_Vk.h"

#define HC_NUM_OF_HEAPS 2

class HC_Vk : public Shader_Vk
{
  private:
    struct alignas(256) InternalCompareParams
    {
        float DiffThreshold = 0.02f;
        float PinkAmount = 1.0f;
        float InvOutputSize[2] = { 0, 0 };
    };

    VkBuffer _buffer[HC_NUM_OF_HEAPS] = {};
    VkDeviceMemory _bufferMemory[HC_NUM_OF_HEAPS] = {};
    VkImageLayout _bufferState[HC_NUM_OF_HEAPS] = { VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL };

  public:
    bool CreateBufferResource(UINT index, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
                              VkImage InSource, VkImageLayout InState);
    void SetBufferState(UINT index, VkCommandBuffer InCommandList, VkImageLayout InState);
    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImage hudless,
                  VkImageLayout state);

    HC_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);

    ~HC_Vk();
};
