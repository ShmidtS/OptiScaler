#pragma once

#include "ffx_api.h"
#include <vulkan/vulkan.h>

ffxReturnCode_t ffxCreateContext_Vk(ffxContext* context, ffxCreateContextDescHeader* desc,
                                    const ffxAllocationCallbacks* memCb);
ffxReturnCode_t ffxDestroyContext_Vk(ffxContext* context, const ffxAllocationCallbacks* memCb);
ffxReturnCode_t ffxConfigure_Vk(ffxContext* context, const ffxConfigureDescHeader* desc);
ffxReturnCode_t ffxQuery_Vk(ffxContext* context, ffxQueryDescHeader* desc);
ffxReturnCode_t ffxDispatch_Vk(ffxContext* context, ffxDispatchDescHeader* desc);

// Frame Generation resource helpers
bool FfxApiVk_HasFGResources();
void FfxApiVk_GetFGResources(VkImage* depth, VkImage* motionVectors, uint32_t* width, uint32_t* height);
void FfxApiVk_SetFGResources(VkImage depth, VkImage motionVectors, uint32_t width, uint32_t height);
