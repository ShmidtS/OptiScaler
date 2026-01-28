#pragma once

#include <pch.h>
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <shaders/Shader_Vk.h>

#define FT_NUM_OF_HEAPS 2

class FT_Vk : public Shader_Vk
{
  private:
    VkImage _buffer = VK_NULL_HANDLE;
    VkDeviceMemory _bufferMemory = VK_NULL_HANDLE;
    VkFormat _format;
    VkImageView _bufferView = VK_NULL_HANDLE;

    uint32_t bufferWidth = 0;
    uint32_t bufferHeight = 0;

    uint32_t InNumThreadsX = 16;
    uint32_t InNumThreadsY = 16;

  public:
    bool CreateBufferResource(VkDevice InDevice, VkImage InSource, VkImageLayout InState);
    bool Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImage InResource, VkImage OutResource);

    VkImage Buffer() { return _buffer; }
    bool CanRender() const { return _init && _buffer != VK_NULL_HANDLE; }
    VkFormat Format() const { return _format; }

    FT_Vk(std::string InName, VkDevice InDevice, VkFormat InFormat);
    FT_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice, VkFormat InFormat);

    bool IsFormatCompatible(VkFormat InFormat);

    ~FT_Vk();
};
