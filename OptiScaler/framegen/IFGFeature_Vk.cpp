#include "IFGFeature_Vk.h"

#include <State.h>
#include <Config.h>

#include <magic_enum.hpp>

bool IFGFeature_Vk::GetResourceCopy(FG_ResourceType type, VkImageLayout bufferState, VkImage output)
{
    if (!InitCopyCmdList())
        return false;

    auto resource = GetResource(type);

    if (resource == nullptr || (resource->copy == VK_NULL_HANDLE && resource->validity == FG_ResourceValidity::ValidNow))
    {
        LOG_WARN("No resource copy of type {} to use", magic_enum::enum_name(type));
        return false;
    }

    auto fIndex = GetIndex();

    if (!_uiCommandListResetted[fIndex])
    {
        if (_copyCommandPool[fIndex] == nullptr)
            return false;

        vkResetCommandPool(_device, _copyCommandPool[fIndex], 0);

        if (_copyCommandList[fIndex] == VK_NULL_HANDLE)
            return false;

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(_copyCommandList[fIndex], &beginInfo);
    }

    if (_copyCommandList[fIndex] == VK_NULL_HANDLE)
        return false;

    // Copy image
    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = static_cast<uint32_t>(resource->width);
    copyRegion.extent.height = static_cast<uint32_t>(resource->height);
    copyRegion.extent.depth = 1;

    vkCmdCopyImage(_copyCommandList[fIndex], resource->GetResource(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   output, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    return true;
}

VkQueue IFGFeature_Vk::GetCommandQueue() { return _gameQueue; }

uint32_t IFGFeature_Vk::GetCommandQueueFamilyIndex() { return _gameQueueFamilyIndex; }

bool IFGFeature_Vk::HasResource(FG_ResourceType type, int index)
{
    std::lock_guard<std::mutex> lock(_frMutex);

    if (index < 0)
        index = GetIndex();

    return _frameResources[index].contains(type);
}

VkCommandBuffer IFGFeature_Vk::GetUICommandList(int index)
{
    if (index < 0)
        index = GetIndex();
    else
        index = index % BUFFER_COUNT;

    LOG_DEBUG("index: {}", index);

    if (_uiCommandPool[0] == nullptr)
    {
        if (_device != nullptr)
            CreateObjects(_device, _physicalDevice);
        else if (State::Instance().currentVkDevice != nullptr)
            CreateObjects(State::Instance().currentVkDevice, State::Instance().currentVkPD);
        else
            return VK_NULL_HANDLE;
    }

    for (size_t j = 0; j < 2; j++)
    {
        auto i = (index + j) % BUFFER_COUNT;

        if (i != index && i < BUFFER_COUNT && _uiCommandListResetted[i])
        {
            if (_uiCommandList[i] == VK_NULL_HANDLE)
            {
                LOG_ERROR("_uiCommandList[{}] is nullptr", i);
                continue;
            }

            LOG_DEBUG("Ending _uiCommandList[{}]: {:X}", i, (size_t) _uiCommandList[i]);
            vkEndCommandBuffer(_uiCommandList[i]);

            _uiCommandListResetted[i] = false;
        }
    }

    if (index >= BUFFER_COUNT)
    {
        LOG_ERROR("Invalid index: {} >= BUFFER_COUNT ({})", index, BUFFER_COUNT);
        return VK_NULL_HANDLE;
    }

    if (!_uiCommandListResetted[index])
    {
        if (_uiCommandPool[index] == nullptr)
        {
            LOG_ERROR("_uiCommandPool[{}] is nullptr", index);
            return VK_NULL_HANDLE;
        }

        vkResetCommandPool(_device, _uiCommandPool[index], 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(_uiCommandList[index], &beginInfo) == VK_SUCCESS)
            _uiCommandListResetted[index] = true;
        else
            LOG_ERROR("_uiCommandList[{}]->Begin() failed", index);
    }

    if (_uiCommandList[index] == VK_NULL_HANDLE)
    {
        LOG_ERROR("_uiCommandList[{}] is nullptr", index);
        return VK_NULL_HANDLE;
    }

    return _uiCommandList[index];
}

VkResource* IFGFeature_Vk::GetResource(FG_ResourceType type, int index)
{
    std::lock_guard<std::mutex> lock(_frMutex);

    if (index < 0)
        index = GetIndex();

    if (!_frameResources[index].contains(type))
        return nullptr;

    auto& currentIndex = _frameResources[index];
    if (auto it = currentIndex.find(type); it != currentIndex.end())
        return &it->second;

    return nullptr;
}

void IFGFeature_Vk::NewFrame()
{
    if (_waitingNewFrameData)
    {
        LOG_DEBUG("Re-activating FG");
        UpdateTarget();
        Activate();
        _waitingNewFrameData = false;
    }

    auto fIndex = GetIndex();

    std::lock_guard<std::mutex> lock(_frMutex);

    LOG_DEBUG("_frameCount: {}, fIndex: {}", _frameCount, fIndex);

    _frameResources[fIndex].clear();
    _uiCommandListResetted[fIndex] = false;
    _lastFGFramePresentId = _fgFramePresentId;
}

void IFGFeature_Vk::FlipResource(VkResource* resource)
{
    auto type = resource->type;

    if (type != FG_ResourceType::Depth && type != FG_ResourceType::Velocity)
        return;

    auto fIndex = GetIndex();
    VkImage flipOutput = VK_NULL_HANDLE;
    std::unique_ptr<RF_Vk>* flip = nullptr;

    flipOutput = _resourceCopy[fIndex][type];

    if (!CreateImageResource(_device, resource->image, VK_IMAGE_LAYOUT_GENERAL, &flipOutput, true,
                             resource->type == FG_ResourceType::Depth))
    {
        LOG_ERROR("{}, CreateImageResource for flip is failed!", magic_enum::enum_name(type));
        return;
    }

    _resourceCopy[fIndex][type] = flipOutput;

    if (type == FG_ResourceType::Depth)
    {
        if (_depthFlip.get() == nullptr)
        {
            _depthFlip = std::make_unique<RF_Vk>("DepthFlip", _device, _physicalDevice);
            return;
        }

        flip = &_depthFlip;
    }
    else
    {
        if (_mvFlip.get() == nullptr)
        {
            _mvFlip = std::make_unique<RF_Vk>("VelocityFlip", _device, _physicalDevice);
            return;
        }

        flip = &_mvFlip;
    }

    if (flip->get()->IsInit())
    {
        auto cmdList = (resource->cmdBuffer != VK_NULL_HANDLE) ? resource->cmdBuffer : GetUICommandList(fIndex);
        auto result = flip->get()->Dispatch(_device, (VkCommandBuffer) cmdList, resource->image,
                                            flipOutput, resource->width, resource->height, true);

        if (result)
        {
            LOG_TRACE("Setting {} from flip, index: {}", magic_enum::enum_name(type), fIndex);
            resource->copy = flipOutput;
            resource->state = VK_IMAGE_LAYOUT_GENERAL;
        }
    }
}

bool IFGFeature_Vk::CreateImageResourceWithSize(VkDevice device, VkImage source,
                                                VkImageLayout state, VkImage* target, UINT width,
                                                UINT height, bool UAV, bool depth)
{
    if (device == VK_NULL_HANDLE || source == VK_NULL_HANDLE)
        return false;

    // For Vulkan, we need to create a new image with similar properties
    // This is a simplified implementation - in production you'd need proper image creation

    if (*target != VK_NULL_HANDLE)
    {
        // Check if existing image matches requirements
        // For now, just recreate
        vkDestroyImage(device, *target, nullptr);
        *target = VK_NULL_HANDLE;
    }

    // Create new image - simplified, would need proper VkImageCreateInfo in production
    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // Default format
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = UAV ? (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                          : (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, target) != VK_SUCCESS)
    {
        LOG_ERROR("vkCreateImage failed");
        return false;
    }

    LOG_DEBUG("Created new one: {}x{}", width, height);

    return true;
}

bool IFGFeature_Vk::InitCopyCmdList()
{
    if (_copyCommandList[0] != VK_NULL_HANDLE && _copyCommandPool[0] != VK_NULL_HANDLE)
        return true;

    if (_device == VK_NULL_HANDLE)
        return false;

    if (_copyCommandList[0] == VK_NULL_HANDLE || _copyCommandPool[0] == VK_NULL_HANDLE)
        DestroyCopyCmdList();

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = _gameQueueFamilyIndex;

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_copyCommandPool[i]) != VK_SUCCESS)
        {
            LOG_ERROR("_copyCommandPool creation failed");
            return false;
        }

        allocInfo.commandPool = _copyCommandPool[i];

        if (vkAllocateCommandBuffers(_device, &allocInfo, &_copyCommandList[i]) != VK_SUCCESS)
        {
            LOG_ERROR("_copyCommandList allocation failed");
            return false;
        }

        // Create fence for synchronization
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(_device, &fenceInfo, nullptr, &_copyFence[i]);
    }

    return true;
}

void IFGFeature_Vk::DestroyCopyCmdList()
{
    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_copyCommandPool[i] != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(_device, _copyCommandPool[i], nullptr);
            _copyCommandPool[i] = VK_NULL_HANDLE;
        }

        if (_copyFence[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(_device, _copyFence[i], nullptr);
            _copyFence[i] = VK_NULL_HANDLE;
        }

        _copyCommandList[i] = VK_NULL_HANDLE;
    }
}

bool IFGFeature_Vk::CreateImageResource(VkDevice device, VkImage source,
                                        VkImageLayout initialState, VkImage* target, bool UAV,
                                        bool depth)
{
    return CreateImageResourceWithSize(device, source, initialState, target, 0, 0, UAV, depth);
}

void IFGFeature_Vk::ImageBarrier(VkCommandBuffer cmdList, VkImage image,
                                 VkImageLayout beforeLayout, VkImageLayout afterLayout,
                                 VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                 VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    if (beforeLayout == afterLayout)
        return;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = beforeLayout;
    barrier.newLayout = afterLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmdList, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool IFGFeature_Vk::CopyResource(VkCommandBuffer cmdList, VkImage source, VkImage* target,
                                 VkImageLayout sourceLayout)
{
    auto result = true;

    ImageBarrier(cmdList, source, sourceLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    if (CreateImageResource(_device, source, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, target))
    {
        VkImageCopy copyRegion = {};
        copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.srcSubresource.layerCount = 1;
        copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.dstSubresource.layerCount = 1;

        vkCmdCopyImage(cmdList, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       *target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
    }
    else
        result = false;

    ImageBarrier(cmdList, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sourceLayout,
                 VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    return result;
}
