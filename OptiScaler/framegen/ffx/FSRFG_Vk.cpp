#include "pch.h"
#include "FSRFG_Vk.h"

#include <vector>

#include <State.h>
#include <Config.h>

#include <magic_enum.hpp>

// Static callback for FG dispatch
static FSRFG_Vk* g_currentFgInstance = nullptr;

static ffxReturnCode_t FgDispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    if (g_currentFgInstance != nullptr)
        return g_currentFgInstance->DispatchCallback(params);
    return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
}

const char* FSRFG_Vk::Name()
{
    return "FSR Frame Generation (Vulkan)";
}

feature_version FSRFG_Vk::Version()
{
    if (_version.major == 0 && FfxApiProxy::IsFGReady())
    {
        _version = { 3, 0, 0 };
    }

    return _version;
}

HWND FSRFG_Vk::Hwnd()
{
    return _hwnd;
}

void* FSRFG_Vk::FrameGenerationContext()
{
    return _fgContext;
}

void* FSRFG_Vk::SwapchainContext()
{
    return _swapChainContext;
}

void FSRFG_Vk::ReleaseObjects()
{
    LOG_FUNC();

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_fgCommandBuffer[i] != VK_NULL_HANDLE && _fgCommandPool[i] != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(_device, _fgCommandPool[i], 1, &_fgCommandBuffer[i]);
            _fgCommandBuffer[i] = VK_NULL_HANDLE;
        }

        if (_fgCommandPool[i] != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(_device, _fgCommandPool[i], nullptr);
            _fgCommandPool[i] = VK_NULL_HANDLE;
        }
    }

    for (size_t i = 0; i < _fgSwapchainImageViews.size(); i++)
    {
        if (_fgSwapchainImageViews[i] != VK_NULL_HANDLE)
            vkDestroyImageView(_device, _fgSwapchainImageViews[i], nullptr);
    }
    _fgSwapchainImageViews.clear();
    _fgSwapchainImages.clear();
}

void FSRFG_Vk::CreateObjects(VkDevice device)
{
    LOG_FUNC();

    if (device == VK_NULL_HANDLE)
        return;

    _device = device;

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &_fgCommandPool[i]) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create FG command pool {}", i);
            continue;
        }

        VkCommandBufferAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _fgCommandPool[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &allocInfo, &_fgCommandBuffer[i]) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to allocate FG command buffer {}", i);
        }
    }

    LOG_INFO("FG Vulkan objects created");
}

bool FSRFG_Vk::CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice,
                                const VkSwapchainCreateInfoKHR* createInfo,
                                VkSwapchainKHR* swapchain)
{
    LOG_FUNC();

    if (!FfxApiProxy::IsVkReady())
    {
        LOG_ERROR("FG API not ready (Vulkan FFX not loaded)!");
        return false;
    }

    // Check if Vulkan FFX version supports Frame Generation (requires 3.2+)
    if (!FfxApiProxy::IsVkFGReady())
    {
        auto version = FfxApiProxy::VersionVk();

        LOG_ERROR("===========================================");
        LOG_ERROR("FFX Vulkan version {}.{}.{} does not support Frame Generation", version.major, version.minor, version.patch);
        LOG_ERROR("Vulkan Frame Generation requires FFX SDK 3.2 or higher");
        LOG_ERROR("");
        LOG_ERROR("To enable Vulkan Frame Generation:");
        LOG_ERROR("  1. Download FFX SDK 3.2+ from:");
        LOG_ERROR("     https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases");
        LOG_ERROR("  2. Place amd_fidelityfx_vk.dll in the game folder");
        LOG_ERROR("  3. Or configure FfxVkPath in OptiScaler.ini to point to the DLL");
        LOG_ERROR("");
        LOG_ERROR("Alternative: Use FGOutput=FSRFG with DX12 interop");
        LOG_ERROR("  OptiScaler can use DX12 Frame Generation with Vulkan games");
        LOG_ERROR("  This requires amd_fidelityfx_dx12.dll or amd_fidelityfx_framegeneration_dx12.dll");
        LOG_ERROR("===========================================");
        LOG_WARN("Vulkan Frame Generation disabled, falling back to standard presentation");
        return false;
    }

    auto version = FfxApiProxy::VersionVk();
    LOG_INFO("FFX Vulkan version {}.{}.{} supports Frame Generation", version.major, version.minor, version.patch);

    _device = device;
    _physicalDevice = physicalDevice;

    _constants.displayWidth = createInfo->imageExtent.width;
    _constants.displayHeight = createInfo->imageExtent.height;

    // Find separate compute queue for async compute
    uint32_t computeQueueFamilyIndex = _queueFamilyIndex;
    VkQueue computeQueue = _gameQueue;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    if (queueFamilyCount > 0)
    {
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        // First try to find a dedicated compute queue (no graphics)
        for (uint32_t i = 0; i < queueFamilyCount; i++)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                VkQueue testQueue = VK_NULL_HANDLE;
                vkGetDeviceQueue(device, i, 0, &testQueue);

                // Only use this queue if the device actually created it
                if (testQueue != VK_NULL_HANDLE)
                {
                    computeQueueFamilyIndex = i;
                    computeQueue = testQueue;
                    LOG_INFO("Found dedicated compute queue family {} with queue {:X}", i, (UINT64)testQueue);
                    break;
                }
                else
                {
                    LOG_DEBUG("Compute-only family {} exists but queue not created by device", i);
                }
            }
        }

        // If no dedicated compute queue, use graphics queue (which also has compute)
        if (computeQueue == _gameQueue)
        {
            LOG_INFO("Using graphics queue for compute (no dedicated compute queue available)");
        }
    }

    LOG_INFO("FG Swapchain params: device={:X}, physicalDevice={:X}, swapchain={:X}",
             (UINT64)device, (UINT64)physicalDevice, (UINT64)*swapchain);
    LOG_INFO("FG Queue params: gameQueue={:X} (family {}), computeQueue={:X} (family {})",
             (UINT64)_gameQueue, _queueFamilyIndex, (UINT64)computeQueue, computeQueueFamilyIndex);

    ffxCreateContextDescFrameGenerationSwapChainVK fgSwapchainDesc {};
    fgSwapchainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    fgSwapchainDesc.physicalDevice = physicalDevice;
    fgSwapchainDesc.device = device;
    fgSwapchainDesc.swapchain = swapchain;
    fgSwapchainDesc.allocator = nullptr; // Use default allocator
    fgSwapchainDesc.createInfo = *createInfo;
    // Preserve oldSwapchain if it exists - FFX needs it for proper swapchain replacement
    // If oldSwapchain is VK_NULL_HANDLE, the current swapchain in *swapchain will be used

    fgSwapchainDesc.gameQueue.queue = _gameQueue;
    fgSwapchainDesc.gameQueue.familyIndex = _queueFamilyIndex;
    fgSwapchainDesc.gameQueue.submitFunc = nullptr;

    // Use dedicated compute queue if available
    fgSwapchainDesc.asyncComputeQueue.queue = computeQueue;
    fgSwapchainDesc.asyncComputeQueue.familyIndex = computeQueueFamilyIndex;
    fgSwapchainDesc.asyncComputeQueue.submitFunc = nullptr;

    fgSwapchainDesc.presentQueue.queue = _gameQueue;
    fgSwapchainDesc.presentQueue.familyIndex = _queueFamilyIndex;
    fgSwapchainDesc.presentQueue.submitFunc = nullptr;

    fgSwapchainDesc.imageAcquireQueue.queue = _gameQueue;
    fgSwapchainDesc.imageAcquireQueue.familyIndex = _queueFamilyIndex;
    fgSwapchainDesc.imageAcquireQueue.submitFunc = nullptr;

    LOG_INFO("Calling FFX VULKAN_CreateContext for FG swapchain...");

    auto result = FfxApiProxy::VULKAN_CreateContext()(&_swapChainContext,
                                                       (ffxCreateContextDescHeader*) &fgSwapchainDesc,
                                                       nullptr);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to create FG swapchain: {} ({})",
                  FfxApiProxy::ReturnCodeToString(result), (UINT) result);
        return false;
    }

    LOG_INFO("FG Vulkan swapchain created successfully");
    return true;
}

bool FSRFG_Vk::ReleaseSwapchain(HWND hwnd)
{
    LOG_FUNC();

    if (_swapChainContext != nullptr)
    {
        FfxApiProxy::VULKAN_DestroyContext()(&_swapChainContext, nullptr);
        _swapChainContext = nullptr;
    }

    return true;
}

bool FSRFG_Vk::CreateContext(VkDevice device, VkPhysicalDevice physicalDevice,
                              VkInstance instance, FG_Constants& fgConstants)
{
    LOG_FUNC();

    _device = device;
    _physicalDevice = physicalDevice;
    _instance = instance;
    _constants = fgConstants;

    if (!FfxApiProxy::IsFGReady())
    {
        LOG_ERROR("FG API not ready!");
        return false;
    }

    ffxCreateContextDescFrameGeneration fgDesc {};
    fgDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    fgDesc.maxRenderSize.width = _maxRenderWidth > 0 ? _maxRenderWidth : fgConstants.displayWidth;
    fgDesc.maxRenderSize.height = _maxRenderHeight > 0 ? _maxRenderHeight : fgConstants.displayHeight;
    fgDesc.displaySize.width = fgConstants.displayWidth;
    fgDesc.displaySize.height = fgConstants.displayHeight;

    if (fgConstants.flags[FG_Flags::Async])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
    if (fgConstants.flags[FG_Flags::DisplayResolutionMVs])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
    if (fgConstants.flags[FG_Flags::JitteredMVs])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
    if (fgConstants.flags[FG_Flags::InvertedDepth])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    if (fgConstants.flags[FG_Flags::InfiniteDepth])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;
    if (fgConstants.flags[FG_Flags::Hdr])
        fgDesc.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

    g_currentFgInstance = this;

    auto result = FfxApiProxy::VULKAN_CreateContext()(&_fgContext,
                                                       (ffxCreateContextDescHeader*) &fgDesc,
                                                       nullptr);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("Failed to create FG context: {:X}", (UINT) result);
        return false;
    }

    CreateObjects(device);
    LOG_INFO("FG Vulkan context created");
    return true;
}

void FSRFG_Vk::Activate()
{
    LOG_FUNC();
    _isActive = true;
}

void FSRFG_Vk::Deactivate()
{
    LOG_FUNC();
    _isActive = false;
}

void FSRFG_Vk::DestroyFGContext()
{
    LOG_FUNC();

    if (_fgContext != nullptr)
    {
        FfxApiProxy::VULKAN_DestroyContext()(&_fgContext, nullptr);
        _fgContext = nullptr;
    }

    ReleaseObjects();
    g_currentFgInstance = nullptr;
}

bool FSRFG_Vk::Shutdown()
{
    LOG_FUNC();

    DestroyFGContext();
    ReleaseSwapchain(_hwnd);

    return true;
}

void FSRFG_Vk::EvaluateState(VkDevice device, FG_Constants& fgConstants)
{
    if (_constants.displayWidth != fgConstants.displayWidth ||
        _constants.displayHeight != fgConstants.displayHeight)
    {
        _constants = fgConstants;
    }
}

bool FSRFG_Vk::Present()
{
    LOG_FUNC();

    if (!_isActive || _fgContext == nullptr || _swapChainContext == nullptr)
        return false;

    auto fIndex = GetIndex();

    if (!HasResource(FG_ResourceType::Depth, fIndex) || !HasResource(FG_ResourceType::Velocity, fIndex))
    {
        LOG_TRACE("Missing depth or velocity resources");
        return false;
    }

    return Dispatch();
}

bool FSRFG_Vk::Dispatch()
{
    LOG_FUNC();

    auto fIndex = GetIndex();
    auto depth = GetResource(FG_ResourceType::Depth, fIndex);
    auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);

    if (depth == nullptr || velocity == nullptr)
        return false;

    // Use FrameGenerationPrepareV2 for depth and motion vectors
    ffxDispatchDescFrameGenerationPrepareV2 prepareDesc {};
    prepareDesc.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prepareDesc.commandList = _fgCommandBuffer[fIndex];
    prepareDesc.frameID = _frameCount;
    prepareDesc.flags = 0;
    prepareDesc.renderSize.width = depth->width;
    prepareDesc.renderSize.height = depth->height;
    prepareDesc.jitterOffset.x = _jitterX[fIndex];
    prepareDesc.jitterOffset.y = _jitterY[fIndex];
    prepareDesc.motionVectorScale.x = _mvScaleX[fIndex];
    prepareDesc.motionVectorScale.y = _mvScaleY[fIndex];
    prepareDesc.frameTimeDelta = static_cast<float>(_ftDelta[fIndex] * 1000.0);
    prepareDesc.cameraNear = _cameraNear[fIndex];
    prepareDesc.cameraFar = _cameraFar[fIndex];
    prepareDesc.cameraFovAngleVertical = _cameraVFov[fIndex];
    prepareDesc.viewSpaceToMetersFactor = _meterFactor[fIndex];
    prepareDesc.reset = _reset[fIndex] != 0;

    // Camera data (V2 structure includes camera info)
    std::memcpy(prepareDesc.cameraPosition, _cameraPosition[fIndex], 3 * sizeof(float));
    std::memcpy(prepareDesc.cameraUp, _cameraUp[fIndex], 3 * sizeof(float));
    std::memcpy(prepareDesc.cameraRight, _cameraRight[fIndex], 3 * sizeof(float));
    std::memcpy(prepareDesc.cameraForward, _cameraForward[fIndex], 3 * sizeof(float));

    // Set depth resource
    prepareDesc.depth.resource = depth->GetImage();
    prepareDesc.depth.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    prepareDesc.depth.description.width = depth->width;
    prepareDesc.depth.description.height = depth->height;
    prepareDesc.depth.description.format = depth->format;
    prepareDesc.depth.description.mipCount = 1;
    prepareDesc.depth.description.flags = 0;
    prepareDesc.depth.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    prepareDesc.depth.state = FFX_API_RESOURCE_STATE_COMPUTE_READ;

    // Set motion vectors
    prepareDesc.motionVectors.resource = velocity->GetImage();
    prepareDesc.motionVectors.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    prepareDesc.motionVectors.description.width = velocity->width;
    prepareDesc.motionVectors.description.height = velocity->height;
    prepareDesc.motionVectors.description.format = velocity->format;
    prepareDesc.motionVectors.description.mipCount = 1;
    prepareDesc.motionVectors.description.flags = 0;
    prepareDesc.motionVectors.description.usage = FFX_API_RESOURCE_USAGE_READ_ONLY;
    prepareDesc.motionVectors.state = FFX_API_RESOURCE_STATE_COMPUTE_READ;

    auto result = FfxApiProxy::VULKAN_Dispatch()(&_fgContext,
                                                  (ffxDispatchDescHeader*) &prepareDesc);

    if (result != FFX_API_RETURN_OK)
    {
        LOG_ERROR("FG prepare dispatch failed: {:X}", (UINT) result);
        return false;
    }

    SetExecuted(fIndex);
    return true;
}

ffxReturnCode_t FSRFG_Vk::DispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    LOG_FUNC();

    if (params == nullptr)
        return FFX_API_RETURN_ERROR_PARAMETER;

    auto fIndex = GetIndex();

    // The frame generation callback is called by FFX to generate frames
    // params contains: presentColor (source), outputs (destination), frameID, etc.

    LOG_DEBUG("Frame generation callback invoked for frame {}, numOutputs: {}",
              params->frameID, params->numGeneratedFrames);

    // Frame generation is handled internally by FFX
    // This callback just needs to return OK to confirm the callback was processed
    // The actual frame generation work is done by FFX using the resources we provided
    // in the Dispatch() function (via FrameGenerationPrepareV2)

    _frameCount++;
    return FFX_API_RETURN_OK;
}

bool FSRFG_Vk::SetResource(VkResource* inputResource)
{
    if (inputResource == nullptr)
        return false;

    auto fIndex = GetIndex();
    std::lock_guard<std::mutex> lock(_frMutex);

    FlipResource(inputResource);
    _frameResources[fIndex][inputResource->type] = *inputResource;

    LOG_DEBUG("Set {} resource for frame {}", magic_enum::enum_name(inputResource->type), fIndex);

    return true;
}

void FSRFG_Vk::SetQueue(FG_ResourceType type, VkQueue queue, uint32_t familyIndex)
{
    _gameQueue = queue;
    _queueFamilyIndex = familyIndex;
}

void FSRFG_Vk::ConfigureFramePaceTuning()
{
    if (_swapChainContext == nullptr)
        return;

    ffxConfigureDescFrameGenerationSwapChainKeyValueVK configDesc {};
    configDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_VK;

    auto frameLimit = Config::Instance()->FramerateLimit.value_or_default();
    if (frameLimit > 0)
    {
        configDesc.key = FFX_API_CONFIGURE_FG_SWAPCHAIN_KEY_FRAMEPACINGTUNING_VK;
        configDesc.u64 = static_cast<uint64_t>(1000000.0 / frameLimit);
        FfxApiProxy::VULKAN_Configure()(&_swapChainContext, (ffxConfigureDescHeader*) &configDesc);
    }
}

bool FSRFG_Vk::ExecuteCommandBuffer(int index)
{
    if (_fgCommandBuffer[index] == VK_NULL_HANDLE)
        return false;

    VkSubmitInfo submitInfo {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_fgCommandBuffer[index];

    auto result = vkQueueSubmit(_gameQueue, 1, &submitInfo, VK_NULL_HANDLE);
    return result == VK_SUCCESS;
}

FSRFG_Vk::~FSRFG_Vk()
{
    Shutdown();
}
