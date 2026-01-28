#include "HC_Vk.h"

#include "../../Logger.h"

HC_Vk::HC_Vk(std::string InName, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
    : Shader_Vk(InName, InDevice, InPhysicalDevice)
{
    LOG_INFO("Creating HC_Vk for {}", InName);
    // TODO: Implement compute shader pipeline for hudless comparison
    _init = true; // Stub initialization
}

HC_Vk::~HC_Vk()
{
    LOG_FUNC();
    // TODO: Cleanup pipeline resources
}

bool HC_Vk::CreateBufferResource(UINT index, VkDevice InDevice, VkPhysicalDevice InPhysicalDevice,
                                 VkImage InSource, VkImageLayout InState)
{
    // TODO: Create buffer for hudless comparison
    return true;
}

void HC_Vk::SetBufferState(UINT index, VkCommandBuffer InCommandList, VkImageLayout InState)
{
    // TODO: Set buffer state with barriers
}

bool HC_Vk::Dispatch(VkDevice InDevice, VkCommandBuffer InCmdList, VkImage hudless,
                     VkImageLayout state)
{
    if (!IsInit())
    {
        LOG_WARN("HC_Vk not initialized");
        return false;
    }

    // TODO: Implement compute shader dispatch for hudless comparison
    LOG_TRACE("HC_Vk::Dispatch called (stub implementation)");
    return true;
}
