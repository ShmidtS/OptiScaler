#pragma once

#include <pch.h>
#include <vulkan/vulkan.h>

#include <framegen/IFGFeature_Vk.h>

namespace ResTrack_Vk
{
    void TrackResource(VkImage image, VkCommandBuffer cmdBuffer, VkImageLayout layout, uint32_t frameId);
    void ClearTracking();
}
