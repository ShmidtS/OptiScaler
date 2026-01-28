#pragma once

#include <pch.h>

#include <vulkan/vulkan.h>
#include "../Shader_Vk.h"

#define RF_NUM_OF_HEAPS 2

class RF_Vk : public Shader_Vk
{
  private:
    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImage InResource,
                  VkImage OutResource, UINT64 width, UINT height, bool velocity);

    RF_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice);

    ~RF_Vk();
};
