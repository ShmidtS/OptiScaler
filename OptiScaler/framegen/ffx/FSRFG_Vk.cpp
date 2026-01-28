#include "FSRFG_Vk.h"

#include <State.h>
#include <Config.h>

#include <menu/menu_overlay_vk.h>

#include <magic_enum.hpp>

// Include FFX API Vulkan headers
#include <vk/ffx_api_vk.h>

#include <algorithm>
#include <vector>

// Helper function to create FfxApiResource from VkResource
static inline FfxApiResource GetFfxResourceFromVkResource(VkResource* resource, uint32_t additionalUsages = 0)
{
    if (resource == nullptr || resource->GetResource() == VK_NULL_HANDLE)
        return FfxApiResource({});

    // Build resource description from VkResource
    FfxApiResourceDescription desc = {};
    desc.width = static_cast<uint32_t>(resource->width);
    desc.height = static_cast<uint32_t>(resource->height);
    desc.format = FFX_API_SURFACE_FORMAT_UNKNOWN; // Will be determined from image info if available
    desc.usage = additionalUsages;
    desc.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;

    // Get state from layout
    uint32_t state = 0;
    switch (resource->state)
    {
    case VK_IMAGE_LAYOUT_GENERAL:
        state = FFX_API_RESOURCE_STATE_COMPUTE_READ;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        state = FFX_API_RESOURCE_STATE_RENDER_TARGET;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        state = FFX_API_RESOURCE_STATE_DEPTH_ATTACHMENT;
        desc.usage |= FFX_API_RESOURCE_USAGE_DEPTHTARGET;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        state = FFX_API_RESOURCE_STATE_PIXEL_READ;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        state = FFX_API_RESOURCE_STATE_COPY_SRC;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        state = FFX_API_RESOURCE_STATE_COPY_DEST;
        break;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
        state = FFX_API_RESOURCE_STATE_PRESENT;
        break;
    default:
        state = FFX_API_RESOURCE_STATE_COMMON;
        break;
    }

    return ffxApiGetResourceVK(resource->GetResource(), desc, state);
}

static inline int GetFormatIndex(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R32G32B32A32_UINT:
    case VK_FORMAT_R32G32B32A32_SINT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 1;

    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SNORM:
    case VK_FORMAT_R16G16B16A16_USCALED:
    case VK_FORMAT_R16G16B16A16_SSCALED:
    case VK_FORMAT_R16G16B16A16_UINT:
    case VK_FORMAT_R16G16B16A16_SINT:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 21;

    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
    case VK_FORMAT_A2B10G10R10_UINT_PACK32:
        return 31;

    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SNORM:
    case VK_FORMAT_R8G8B8A8_USCALED:
    case VK_FORMAT_R8G8B8A8_SSCALED:
    case VK_FORMAT_R8G8B8A8_UINT:
    case VK_FORMAT_R8G8B8A8_SINT:
        return 51;
    }

    return -1;
}

static inline bool FormatsCompatible(VkFormat format1, VkFormat format2)
{
    if (format1 == format2)
        return true;

    auto fi1 = GetFormatIndex(format1);
    if (fi1 < 0)
        return false;

    auto fi2 = GetFormatIndex(format2);
    if (fi2 < 0)
        return false;

    if (fi1 == fi2)
        return true;

    if ((fi1 - 1 == fi2) || (fi2 - 1 == fi1))
        return true;

    return false;
}

bool FSRFG_Vk::HudlessFormatTransfer(int index, VkDevice device, VkFormat targetFormat, VkResource* resource)
{
    if (_hudlessTransfer[index].get() == nullptr || !_hudlessTransfer[index].get()->IsFormatCompatible(targetFormat))
    {
        LOG_DEBUG("Format change, recreate the FormatTransfer");

        if (_hudlessTransfer[index].get() != nullptr)
            _hudlessTransfer[index].reset();

        _hudlessTransfer[index] = std::make_unique<FT_Vk>("FormatTransfer", device, targetFormat);

        return false;
    }

    if (_hudlessTransfer[index].get() != nullptr &&
        _hudlessTransfer[index].get()->CreateBufferResource(device, resource->image, VK_IMAGE_LAYOUT_GENERAL))
    {
        auto cmdList = GetUICommandList(index);

        if (resource->cmdBuffer != VK_NULL_HANDLE && _hudlessCopyResource[index] != VK_NULL_HANDLE)
        {
            ImageBarrier(resource->cmdBuffer, resource->image, resource->state,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageCopy copyRegion = {};
            copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.srcSubresource.layerCount = 1;
            copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.dstSubresource.layerCount = 1;
            copyRegion.extent.width = static_cast<uint32_t>(resource->width);
            copyRegion.extent.height = static_cast<uint32_t>(resource->height);
            copyRegion.extent.depth = 1;

            vkCmdCopyImage(resource->cmdBuffer, resource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           _hudlessCopyResource[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            ImageBarrier(resource->cmdBuffer, resource->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         resource->state, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            ImageBarrier(resource->cmdBuffer, _hudlessCopyResource[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            _hudlessTransfer[index].get()->Dispatch(device, cmdList, _hudlessCopyResource[index],
                                                    _hudlessTransfer[index].get()->Buffer());

            ImageBarrier(cmdList, _hudlessCopyResource[index], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }
        else
        {
            ImageBarrier(cmdList, resource->image, resource->state,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            _hudlessTransfer[index].get()->Dispatch(device, cmdList, resource->image,
                                                    _hudlessTransfer[index].get()->Buffer());

            ImageBarrier(cmdList, resource->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         resource->state, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        resource->copy = _hudlessTransfer[index].get()->Buffer();
        resource->state = VK_IMAGE_LAYOUT_GENERAL;

        return true;
    }

    return false;
}

bool FSRFG_Vk::UIFormatTransfer(int index, VkDevice device, VkCommandBuffer cmdList, VkFormat targetFormat, VkResource* resource)
{
    if (_uiTransfer[index].get() == nullptr || !_uiTransfer[index].get()->IsFormatCompatible(targetFormat))
    {
        LOG_DEBUG("Format change, recreate the FormatTransfer");

        if (_uiTransfer[index].get() != nullptr)
            _uiTransfer[index].reset();

        _uiTransfer[index] = std::make_unique<FT_Vk>("FormatTransfer", device, targetFormat);

        return false;
    }

    if (_uiTransfer[index].get() != nullptr &&
        _uiTransfer[index].get()->CreateBufferResource(device, resource->image, VK_IMAGE_LAYOUT_GENERAL))
    {
        ImageBarrier(cmdList, resource->image, resource->state,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        _uiTransfer[index].get()->Dispatch(device, cmdList, resource->image,
                                           _uiTransfer[index].get()->Buffer());

        ImageBarrier(cmdList, resource->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     resource->state, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        resource->copy = _uiTransfer[index].get()->Buffer();
        return true;
    }

    return false;
}

void FSRFG_Vk::ConfigureFramePaceTuning()
{
    State::Instance().FSRFGFTPchanged = false;

    if (_swapChainContext == nullptr || Version() < feature_version { 3, 1, 3 })
        return;

    // Vulkan implementation would go here when FFX API supports it
}

feature_version FSRFG_Vk::Version()
{
    if (_fgContext == nullptr && _version.major == 0)
    {
        if (!FfxApiProxy::IsFGReady())
            FfxApiProxy::InitFfxVk();

        if (FfxApiProxy::IsFGReady())
            _version = FfxApiProxy::VersionVk();
    }

    return _version;
}

HWND FSRFG_Vk::Hwnd() { return _hwnd; }

const char* FSRFG_Vk::Name() { return "FSR-FG-Vk"; }

static void fgLogCallback(uint32_t type, const wchar_t* message)
{
    auto message_str = wstring_to_string(std::wstring(message));

    if (type == FFX_API_MESSAGE_TYPE_ERROR)
        spdlog::error("FFX FG Callback: {}", message_str);
    else if (type == FFX_API_MESSAGE_TYPE_WARNING)
        spdlog::warn("FFX FG Callback: {}", message_str);
}

bool FSRFG_Vk::Dispatch()
{
    LOG_DEBUG();

    if (_fgContext == nullptr)
    {
        LOG_DEBUG("No fg context");
        return false;
    }

    UINT64 willDispatchFrame = 0;
    auto fIndex = GetDispatchIndex(willDispatchFrame);
    if (fIndex < 0)
        return false;

    if (!IsActive() || IsPaused())
        return false;

    auto& state = State::Instance();
    auto config = Config::Instance();

    if (state.FSRFGFTPchanged)
        ConfigureFramePaceTuning();

    LOG_DEBUG("_frameCount: {}, willDispatchFrame: {}, fIndex: {}", _frameCount, willDispatchFrame, fIndex);

    if (!_resourceReady[fIndex].contains(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Depth) ||
        !_resourceReady[fIndex].contains(FG_ResourceType::Velocity) ||
        !_resourceReady[fIndex].at(FG_ResourceType::Velocity))
    {
        LOG_WARN("Depth or Velocity is not ready, skipping");
        return false;
    }

    ffxConfigureDescFrameGeneration fgConfig = {};
    fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;

    ffxConfigureDescFrameGenerationRegisterDistortionFieldResource distortionFieldDesc {};
    distortionFieldDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE;

    auto distortion = GetResource(FG_ResourceType::Distortion, fIndex);
    if (distortion != nullptr && IsResourceReady(FG_ResourceType::Distortion, fIndex))
    {
        LOG_TRACE("Using Distortion Field: {:X}", (size_t) distortion->GetResource());

        distortionFieldDesc.distortionField = GetFfxResourceFromVkResource(distortion);

        distortionFieldDesc.header.pNext = fgConfig.header.pNext;
        fgConfig.header.pNext = &distortionFieldDesc.header;
    }

    ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceVK uiDesc {};
    uiDesc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FGSWAPCHAIN_REGISTERUIRESOURCE_VK;

    auto uiColor = GetResource(FG_ResourceType::UIColor, fIndex);
    auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
    if (uiColor != nullptr && IsResourceReady(FG_ResourceType::UIColor, fIndex) &&
        config->FGDrawUIOverFG.value_or_default())
    {
        LOG_TRACE("Using UI: {:X}", (size_t) uiColor->GetResource());

        uiDesc.uiResource = GetFfxResourceFromVkResource(uiColor);

        if (config->FGUIPremultipliedAlpha.value_or_default())
            uiDesc.flags = FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_USE_PREMUL_ALPHA;
    }
    else if (hudless != nullptr && IsResourceReady(FG_ResourceType::HudlessColor, fIndex))
    {
        LOG_TRACE("Using hudless: {:X}", (size_t) hudless->GetResource());

        uiDesc.uiResource = FfxApiResource({});
        fgConfig.HUDLessColor = GetFfxResourceFromVkResource(hudless);
    }
    else
    {
        uiDesc.uiResource = FfxApiResource({});
        fgConfig.HUDLessColor = FfxApiResource({});
    }

    FfxApiProxy::VK_GetConfigure()(&_swapChainContext, &uiDesc.header);

    if (fgConfig.HUDLessColor.resource != nullptr)
    {
        static auto localLastHudlessFormat = (FfxApiSurfaceFormat) fgConfig.HUDLessColor.description.format;
        _lastHudlessFormat = (FfxApiSurfaceFormat) fgConfig.HUDLessColor.description.format;

        if (localLastHudlessFormat != _lastHudlessFormat)
        {
            state.FGchanged = true;
            state.SCchanged = true;
            LOG_DEBUG("HUDLESS format changed, triggering FG reinit");
        }

        localLastHudlessFormat = _lastHudlessFormat;
    }

    fgConfig.frameGenerationEnabled = _isActive;
    fgConfig.flags = 0;

    if (config->FGDebugView.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_VIEW;

    if (config->FGDebugTearLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_TEAR_LINES;

    if (config->FGDebugResetLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_RESET_INDICATORS;

    if (config->FGDebugPacingLines.value_or_default())
        fgConfig.flags |= FFX_FRAMEGENERATION_FLAG_DRAW_DEBUG_PACING_LINES;

    fgConfig.allowAsyncWorkloads = config->FGAsync.value_or_default();

    {
        VkExtent2D swapChainExtent = _swapChainExtent;

        int bufferWidth = swapChainExtent.width;
        int bufferHeight = swapChainExtent.height;

        int defaultLeft = 0;
        int defaultTop = 0;
        int defaultWidth = 0;
        int defaultHeight = 0;

        defaultLeft = static_cast<int>(bufferWidth - _interpolationWidth[fIndex]) / 2;
        defaultTop = static_cast<int>(bufferHeight - _interpolationHeight[fIndex]) / 2;
        defaultWidth = static_cast<int>(_interpolationWidth[fIndex]);
        defaultHeight = _interpolationHeight[fIndex];

        fgConfig.generationRect.left = config->FGRectLeft.value_or(_interpolationLeft[fIndex].value_or(defaultLeft));
        fgConfig.generationRect.top = config->FGRectTop.value_or(_interpolationTop[fIndex].value_or(defaultTop));
        fgConfig.generationRect.width = config->FGRectWidth.value_or(defaultWidth);
        fgConfig.generationRect.height = config->FGRectHeight.value_or(defaultHeight);
    }

    fgConfig.frameGenerationCallbackUserContext = this;
    fgConfig.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* pUserCtx) -> ffxReturnCode_t
    {
        FSRFG_Vk* fsrFG = nullptr;

        if (pUserCtx != nullptr)
            fsrFG = reinterpret_cast<FSRFG_Vk*>(pUserCtx);

        if (fsrFG != nullptr)
            return fsrFG->DispatchCallback(params);

        return FFX_API_RETURN_ERROR;
    };

    fgConfig.onlyPresentGenerated = state.FGonlyGenerated;
    fgConfig.frameID = willDispatchFrame;
    fgConfig.swapChain = reinterpret_cast<void*>(_swapChain);

    ffxReturnCode_t retCode = FfxApiProxy::VK_GetConfigure()(&_fgContext, &fgConfig.header);
    LOG_DEBUG("VK_Configure result: {0:X}, frame: {1}, fIndex: {2}", retCode, willDispatchFrame, fIndex);

    ffxConfigureDescGlobalDebug1 fgLogging = {};
    fgLogging.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG1;
    fgLogging.fpMessage = &fgLogCallback;
    fgLogging.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    ffxReturnCode_t loggingRetCode = FfxApiProxy::VK_GetConfigure()(&_fgContext, &fgLogging.header);

    bool dispatchResult = false;
    if (retCode == FFX_API_RETURN_OK && _isActive)
    {
        ffxCreateBackendVKDesc backendDesc {};
        backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
        backendDesc.vkDevice = _device;
        backendDesc.vkPhysicalDevice = _physicalDevice;
        backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

        ffxDispatchDescFrameGenerationPrepareCameraInfo dfgCameraData {};
        dfgCameraData.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
        dfgCameraData.header.pNext = &backendDesc.header;

        std::memcpy(dfgCameraData.cameraPosition, _cameraPosition[fIndex], 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraUp, _cameraUp[fIndex], 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraRight, _cameraRight[fIndex], 3 * sizeof(float));
        std::memcpy(dfgCameraData.cameraForward, _cameraForward[fIndex], 3 * sizeof(float));

        ffxDispatchDescFrameGenerationPrepare dfgPrepare {};
        dfgPrepare.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE;
        dfgPrepare.header.pNext = &dfgCameraData.header;

        // Prepare command list
        auto allocator = _fgCommandPool[fIndex];
        vkResetCommandPool(_device, allocator, 0);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(_fgCommandList[fIndex], &beginInfo);

        dfgPrepare.commandList = _fgCommandList[fIndex];
        dfgPrepare.frameID = willDispatchFrame;
        dfgPrepare.flags = fgConfig.flags;

        auto velocity = GetResource(FG_ResourceType::Velocity, fIndex);
        auto depth = GetResource(FG_ResourceType::Depth, fIndex);

        if (velocity != nullptr && IsResourceReady(FG_ResourceType::Velocity, fIndex))
        {
            LOG_DEBUG("Velocity resource: {:X}", (size_t) velocity->GetResource());
            dfgPrepare.motionVectors = GetFfxResourceFromVkResource(velocity);
        }
        else
        {
            LOG_ERROR("Velocity is missing");
            vkEndCommandBuffer(_fgCommandList[fIndex]);
            return false;
        }

        if (depth != nullptr && IsResourceReady(FG_ResourceType::Depth, fIndex))
        {
            LOG_DEBUG("Depth resource: {:X}", (size_t) depth->GetResource());
            dfgPrepare.depth = GetFfxResourceFromVkResource(depth, FFX_API_RESOURCE_USAGE_DEPTHTARGET);
        }
        else
        {
            LOG_ERROR("Depth is missing");
            vkEndCommandBuffer(_fgCommandList[fIndex]);
            return false;
        }

        if (state.currentFeature && state.activeFgInput == FGInput::Upscaler)
            dfgPrepare.renderSize = { state.currentFeature->RenderWidth(), state.currentFeature->RenderHeight() };
        else if (depth != nullptr)
            dfgPrepare.renderSize = { static_cast<uint32_t>(depth->width), depth->height };
        else
            dfgPrepare.renderSize = { dfgPrepare.depth.description.width, dfgPrepare.depth.description.height };

        dfgPrepare.jitterOffset.x = _jitterX[fIndex];
        dfgPrepare.jitterOffset.y = _jitterY[fIndex];
        dfgPrepare.motionVectorScale.x = _mvScaleX[fIndex];
        dfgPrepare.motionVectorScale.y = _mvScaleY[fIndex];
        dfgPrepare.cameraFar = _cameraFar[fIndex];
        dfgPrepare.cameraNear = _cameraNear[fIndex];
        dfgPrepare.cameraFovAngleVertical = _cameraVFov[fIndex];
        dfgPrepare.frameTimeDelta = static_cast<float>(state.lastFGFrameTime);
        dfgPrepare.viewSpaceToMetersFactor = _meterFactor[fIndex];

        retCode = FfxApiProxy::VK_GetDispatch()(&_fgContext, &dfgPrepare.header);
        LOG_DEBUG("VK_Dispatch result: {0}, frame: {1}, fIndex: {2}, commandList: {3:X}", retCode, willDispatchFrame,
                  fIndex, (size_t) dfgPrepare.commandList);

        if (retCode == FFX_API_RETURN_OK)
        {
            vkEndCommandBuffer(_fgCommandList[fIndex]);
            _waitingExecute[fIndex] = true;
            dispatchResult = ExecuteCommandList(fIndex);
        }
    }

    if (config->FGUseMutexForSwapchain.value_or_default() && Mutex.getOwner() == 1)
    {
        LOG_TRACE("Releasing FG->Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    };

    return dispatchResult;
}

ffxReturnCode_t FSRFG_Vk::DispatchCallback(ffxDispatchDescFrameGeneration* params)
{
    const int fIndex = params->frameID % BUFFER_COUNT;

    auto& state = State::Instance();

    if (!Config::Instance()->FGSkipReset.value_or_default())
        params->reset = (_reset[fIndex] != 0);
    else
        params->reset = 0;

    LOG_DEBUG("frameID: {}, commandList: {:X}, numGeneratedFrames: {}", params->frameID, (size_t) params->commandList,
              params->numGeneratedFrames);

    // check for status
    if (!Config::Instance()->FGEnabled.value_or_default() || _fgContext == nullptr || state.SCchanged)
    {
        LOG_WARN("Cancel async dispatch");
        params->numGeneratedFrames = 0;
    }

    // If fg is active but upscaling paused
    if ((state.currentFeature == nullptr && state.activeFgInput == FGInput::Upscaler) || state.FGchanged ||
        fIndex < 0 || !IsActive() || (state.currentFeature && state.currentFeature->FrameCount() == 0))
    {
        LOG_WARN("Upscaling paused! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    static UINT64 _lastFrameId = 0;
    if (params->frameID == _lastFrameId)
    {
        LOG_WARN("Dispatched with the same frame id! frameID: {}", params->frameID);
        params->numGeneratedFrames = 0;
    }

    auto scFormat = (FfxApiSurfaceFormat) params->presentColor.description.format;
    auto lhFormat = _lastHudlessFormat;
    auto uhFormat = _usingHudlessFormat;

    if (_lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN && lhFormat != scFormat &&
        (_usingHudlessFormat == FFX_API_SURFACE_FORMAT_UNKNOWN || uhFormat != lhFormat))
    {
        LOG_DEBUG("Hudless format doesn't match, hudless: {}, present: {}", (uint32_t) _lastHudlessFormat,
                  params->presentColor.description.format);

        params->numGeneratedFrames = 0;
        _lastFrameId = params->frameID;

        state.FGchanged = true;
        state.SCchanged = true;

        return FFX_API_RETURN_OK;
    }

    auto dispatchResult = FfxApiProxy::VK_GetDispatch()(&_fgContext, &params->header);
    LOG_DEBUG("VK_Dispatch result: {}, fIndex: {}", (UINT) dispatchResult, fIndex);

    _lastFrameId = params->frameID;

    return dispatchResult;
}

FSRFG_Vk::~FSRFG_Vk() { Shutdown(); }

void* FSRFG_Vk::FrameGenerationContext()
{
    LOG_DEBUG("");
    return (void*) _fgContext;
}

void* FSRFG_Vk::SwapchainContext()
{
    LOG_DEBUG("");
    return _swapChainContext;
}

void FSRFG_Vk::DestroyFGContext()
{
    _frameCount = 1;
    _version = {};

    LOG_DEBUG("");

    Deactivate();

    if (_fgContext != nullptr)
    {
        auto result = FfxApiProxy::VK_GetDestroyContext()(&_fgContext, nullptr);

        if (!(State::Instance().isShuttingDown))
            LOG_INFO("VK_DestroyContext result: {0:X}", result);

        _fgContext = nullptr;
    }

    ReleaseObjects();
}

bool FSRFG_Vk::Shutdown()
{
    Deactivate();

    if (_swapChainContext != nullptr)
    {
        if (ReleaseSwapchain(_hwnd))
            State::Instance().currentVkFGSwapchain = nullptr;
    }

    ReleaseObjects();

    return true;
}

bool FSRFG_Vk::CreateSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                               VkSurfaceKHR surface, VkSwapchainKHR* swapChain)
{
    if (State::Instance().currentVkFGSwapchain != nullptr && _hwnd != NULL)
    {
        LOG_WARN("FG swapchain already created for the same output window!");
        // Note: Vulkan swapchain recreation would be handled differently
        return true;
    }

    // Get queue info from Streamline inputs
    VkQueue gameQueue = State::Instance().slFGInputsVk.GetGameQueue();
    uint32_t gameQueueFamilyIndex = State::Instance().slFGInputsVk.GetGameQueueFamilyIndex();

    if (gameQueue == VK_NULL_HANDLE)
    {
        LOG_ERROR("Game queue not set! FG cannot be initialized without a valid queue.");
        return false;
    }

    LOG_DEBUG("Using game queue: {:X}, family index: {}", (size_t)gameQueue, gameQueueFamilyIndex);

    // Vulkan swapchain creation through FFX API
    // Note: The FFX SDK doesn't have a direct equivalent to DX12's FrameGenerationSwapChainNew
    // We need to use the standard swapchain replacement approach

    // Get surface capabilities to fill createInfo
    VkSurfaceCapabilitiesKHR surfaceCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps);

    // Get surface formats
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    if (formatCount > 0)
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

    // Get present modes
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    if (presentModeCount > 0)
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    // Store the original swapchain handle
    VkSwapchainKHR originalSwapchain = *swapChain;

    ffxCreateContextDescFrameGenerationSwapChainVK createSwapChainDesc {};
    createSwapChainDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FGSWAPCHAIN_VK;
    createSwapChainDesc.physicalDevice = physicalDevice;
    createSwapChainDesc.device = device;
    // Pass VK_NULL_HANDLE - FFX will create a new swapchain and return it in this pointer
    // The original swapchain will be managed by the game
    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    createSwapChainDesc.swapchain = &newSwapchain;

    // Fill createInfo with reasonable defaults from surface capabilities
    createSwapChainDesc.createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createSwapChainDesc.createInfo.surface = surface;
    createSwapChainDesc.createInfo.minImageCount = std::max(3u, surfaceCaps.minImageCount); // FG needs at least 3 buffers
    if (surfaceCaps.maxImageCount > 0)
        createSwapChainDesc.createInfo.minImageCount = std::min(createSwapChainDesc.createInfo.minImageCount, surfaceCaps.maxImageCount);
    // Select a suitable surface format (prefer B8G8R8A8_UNORM or R8G8B8A8_UNORM)
    createSwapChainDesc.createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    createSwapChainDesc.createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (const auto& fmt : surfaceFormats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM || fmt.format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            createSwapChainDesc.createInfo.imageFormat = fmt.format;
            createSwapChainDesc.createInfo.imageColorSpace = fmt.colorSpace;
            break;
        }
    }
    createSwapChainDesc.createInfo.imageExtent = surfaceCaps.currentExtent;
    createSwapChainDesc.createInfo.imageArrayLayers = 1;
    createSwapChainDesc.createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    createSwapChainDesc.createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createSwapChainDesc.createInfo.preTransform = surfaceCaps.currentTransform;
    createSwapChainDesc.createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Prefer FIFO for vsync, but allow immediate if FIFO not available
    createSwapChainDesc.createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes)
    {
        if (mode == VK_PRESENT_MODE_FIFO_KHR)
        {
            createSwapChainDesc.createInfo.presentMode = mode;
            break;
        }
    }
    createSwapChainDesc.createInfo.clipped = VK_TRUE;
    // Don't pass old swapchain - let FFX create a completely new one
    createSwapChainDesc.createInfo.oldSwapchain = VK_NULL_HANDLE;

    // Fill queue info - all queues use the same game queue for simplicity
    // FFX API will use these queues for different operations
    createSwapChainDesc.gameQueue.queue = gameQueue;
    createSwapChainDesc.gameQueue.familyIndex = gameQueueFamilyIndex;
    createSwapChainDesc.gameQueue.submitFunc = nullptr;

    createSwapChainDesc.asyncComputeQueue.queue = gameQueue;
    createSwapChainDesc.asyncComputeQueue.familyIndex = gameQueueFamilyIndex;
    createSwapChainDesc.asyncComputeQueue.submitFunc = nullptr;

    createSwapChainDesc.presentQueue.queue = gameQueue;
    createSwapChainDesc.presentQueue.familyIndex = gameQueueFamilyIndex;
    createSwapChainDesc.presentQueue.submitFunc = nullptr;

    createSwapChainDesc.imageAcquireQueue.queue = gameQueue;
    createSwapChainDesc.imageAcquireQueue.familyIndex = gameQueueFamilyIndex;
    createSwapChainDesc.imageAcquireQueue.submitFunc = nullptr;

    LOG_DEBUG("Creating FFX FG swapchain with queues - game: {:X}, family: {}",
              (size_t)gameQueue, gameQueueFamilyIndex);

    auto result = FfxApiProxy::VK_GetCreateContext()(&_swapChainContext, &createSwapChainDesc.header, nullptr);

    if (result == FFX_API_RETURN_OK)
    {
        ConfigureFramePaceTuning();

        _gameQueue = gameQueue;
        _gameQueueFamilyIndex = gameQueueFamilyIndex;
        // FFX creates a new swapchain and returns it in newSwapchain
        // We need to update the game's swapchain pointer to use the FFX one
        if (newSwapchain != VK_NULL_HANDLE)
        {
            LOG_DEBUG("FFX created new swapchain: {:X}, original: {:X}", (size_t)newSwapchain, (size_t)originalSwapchain);
            *swapChain = newSwapchain;
            _swapChain = newSwapchain;
        }
        else
        {
            LOG_WARN("FFX returned null swapchain, using original");
            _swapChain = originalSwapchain;
        }
        // _hwnd = desc->OutputWindow; // Vulkan doesn't use HWND directly

        LOG_INFO("FG swapchain created successfully");
        return true;
    }

    LOG_ERROR("FFX VK_CreateContext for swapchain failed with result: {:X} ({})",
              result, FfxApiProxy::ReturnCodeToString(result));
    return false;
}

bool FSRFG_Vk::ReleaseSwapchain(HWND hwnd)
{
    if (hwnd != _hwnd || _hwnd == NULL)
        return false;

    LOG_DEBUG("");

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Waiting Mutex 1, current: {}", Mutex.getOwner());
        Mutex.lock(1);
        LOG_TRACE("Accuired Mutex: {}", Mutex.getOwner());
    }

    MenuOverlayVk::DestroyVulkanObjects(true);

    if (_fgContext != nullptr)
        DestroyFGContext();

    if (_swapChainContext != nullptr)
    {
        auto result = FfxApiProxy::VK_GetDestroyContext()(&_swapChainContext, nullptr);
        LOG_INFO("Destroy Ffx Swapchain Result: {}({})", result, FfxApiProxy::ReturnCodeToString(result));

        _swapChainContext = nullptr;
        State::Instance().currentVkFGSwapchain = nullptr;
    }

    if (Config::Instance()->FGUseMutexForSwapchain.value_or_default())
    {
        LOG_TRACE("Releasing Mutex: {}", Mutex.getOwner());
        Mutex.unlockThis(1);
    }

    return true;
}

void FSRFG_Vk::CreateContext(VkDevice device, VkPhysicalDevice physicalDevice, FG_Constants& fgConstants)
{
    LOG_DEBUG("");

    CreateObjects(device, physicalDevice);

    _constants = fgConstants;

    // Changing the format of the hudless resource requires a new context
    if (_fgContext != nullptr && (_lastHudlessFormat != _usingHudlessFormat))
    {
        auto result = FfxApiProxy::VK_GetDestroyContext()(&_fgContext, nullptr);
        _fgContext = nullptr;
    }

    if (_fgContext != nullptr)
    {
        ffxConfigureDescFrameGeneration m_FrameGenerationConfig = {};
        m_FrameGenerationConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        m_FrameGenerationConfig.frameGenerationEnabled = true;
        m_FrameGenerationConfig.swapChain = reinterpret_cast<void*>(_swapChain);
        m_FrameGenerationConfig.presentCallback = nullptr;
        m_FrameGenerationConfig.HUDLessColor = FfxApiResource({});

        auto result = FfxApiProxy::VK_GetConfigure()(&_fgContext, &m_FrameGenerationConfig.header);

        _isActive = (result == FFX_API_RETURN_OK);

        LOG_DEBUG("Reactivate");

        return;
    }

    ffxQueryDescGetVersions versionQuery {};
    versionQuery.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versionQuery.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    versionQuery.device = _physicalDevice;
    uint64_t versionCount = 0;
    versionQuery.outputCount = &versionCount;
    FfxApiProxy::VK_GetQuery()(nullptr, &versionQuery.header);

    State::Instance().ffxFGVersionIds.resize(versionCount);
    State::Instance().ffxFGVersionNames.resize(versionCount);
    versionQuery.versionIds = State::Instance().ffxFGVersionIds.data();
    versionQuery.versionNames = State::Instance().ffxFGVersionNames.data();
    FfxApiProxy::VK_GetQuery()(nullptr, &versionQuery.header);

    ffxCreateBackendVKDesc backendDesc {};
    backendDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backendDesc.vkDevice = device;
    backendDesc.vkPhysicalDevice = _physicalDevice;
    backendDesc.vkDeviceProcAddr = vkGetDeviceProcAddr;

    ffxCreateContextDescFrameGenerationHudless hudlessDesc {};
    hudlessDesc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_HUDLESS;
    hudlessDesc.hudlessBackBufferFormat = _lastHudlessFormat;
    hudlessDesc.header.pNext = &backendDesc.header;

    ffxCreateContextDescFrameGeneration createFg {};
    createFg.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;

    // Use swapchain extent info
    if (_swapChainExtent.width != 0 && _swapChainExtent.height != 0)
    {
        createFg.displaySize = { _swapChainExtent.width, _swapChainExtent.height };

        if (fgConstants.displayWidth != 0 && fgConstants.displayHeight != 0)
            createFg.maxRenderSize = { fgConstants.displayWidth, fgConstants.displayHeight };
        else
            createFg.maxRenderSize = { _swapChainExtent.width, _swapChainExtent.height };
    }
    else
    {
        createFg.displaySize = { fgConstants.displayWidth, fgConstants.displayHeight };
        createFg.maxRenderSize = { fgConstants.displayWidth, fgConstants.displayHeight };
    }

    _maxRenderWidth = createFg.maxRenderSize.width;
    _maxRenderHeight = createFg.maxRenderSize.height;

    createFg.flags = 0;

    if (fgConstants.flags & FG_Flags::Hdr)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;

    if (fgConstants.flags & FG_Flags::InvertedDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;

    if (fgConstants.flags & FG_Flags::JitteredMVs)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;

    if (fgConstants.flags & FG_Flags::DisplayResolutionMVs)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;

    if (fgConstants.flags & FG_Flags::Async)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;

    if (fgConstants.flags & FG_Flags::InfiniteDepth)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE;

    if (spdlog::default_logger()->level() == SPDLOG_LEVEL_TRACE)
        createFg.flags |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;

    // Vulkan format would need to be converted from VkFormat
    createFg.backBufferFormat = FFX_API_SURFACE_FORMAT_UNKNOWN; // TODO: Convert from swapchain format

    if (_lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN)
    {
        _usingHudlessFormat = _lastHudlessFormat;
        _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
        createFg.header.pNext = &hudlessDesc.header;
    }
    else
    {
        _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
        createFg.header.pNext = &backendDesc.header;
    }

    {
        ScopedSkipSpoofing skipSpoofing {};
        ScopedSkipHeapCapture skipHeapCapture {};

        if (Config::Instance()->FfxFGIndex.value_or_default() < 0 ||
            Config::Instance()->FfxFGIndex.value_or_default() >= State::Instance().ffxFGVersionIds.size())
            Config::Instance()->FfxFGIndex.set_volatile_value(0);

        ffxOverrideVersion override = { 0 };
        override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
        override.versionId = State::Instance().ffxFGVersionIds[Config::Instance()->FfxFGIndex.value_or_default()];
        backendDesc.header.pNext = &override.header;

        ParseVersion(State::Instance().ffxFGVersionNames[Config::Instance()->FfxFGIndex.value_or_default()], &_version);

        ffxReturnCode_t retCode = FfxApiProxy::VK_GetCreateContext()(&_fgContext, &createFg.header, nullptr);

        LOG_INFO("VK_CreateContext result: {:X}", retCode);
        _isActive = (retCode == FFX_API_RETURN_OK);
        _lastDispatchedFrame = 0;
    }

    LOG_DEBUG("Create");
}

void FSRFG_Vk::Activate()
{
    if (_fgContext != nullptr && _swapChain != VK_NULL_HANDLE && !_isActive)
    {
        ffxConfigureDescFrameGeneration fgConfig = {};
        fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        fgConfig.frameGenerationEnabled = true;
        fgConfig.swapChain = reinterpret_cast<void*>(_swapChain);
        fgConfig.presentCallback = nullptr;
        fgConfig.HUDLessColor = FfxApiResource({});

        auto result = FfxApiProxy::VK_GetConfigure()(&_fgContext, &fgConfig.header);

        if (result == FFX_API_RETURN_OK)
        {
            _isActive = true;
            _lastDispatchedFrame = 0;
        }

        LOG_INFO("VK_Configure Enabled: true, result: {} ({})", magic_enum::enum_name((FfxApiReturnCodes) result),
                 (UINT) result);
    }
}

void FSRFG_Vk::Deactivate()
{
    if (_isActive)
    {
        auto fIndex = GetIndex();
        if (_uiCommandListResetted[fIndex])
        {
            LOG_DEBUG("Executing _uiCommandList[{}]: {:X}", fIndex, (size_t) _uiCommandList[fIndex]);
            vkEndCommandBuffer(_uiCommandList[fIndex]);

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &_uiCommandList[fIndex];

            vkQueueSubmit(_gameQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(_gameQueue);

            _uiCommandListResetted[fIndex] = false;
        }

        ffxReturnCode_t result = FFX_API_RETURN_OK;

        if (_fgContext != nullptr)
        {
            ffxConfigureDescFrameGeneration fgConfig = {};
            fgConfig.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
            fgConfig.frameGenerationEnabled = false;
            fgConfig.swapChain = reinterpret_cast<void*>(_swapChain);
            fgConfig.presentCallback = nullptr;
            fgConfig.HUDLessColor = FfxApiResource({});

            auto result = FfxApiProxy::VK_GetConfigure()(&_fgContext, &fgConfig.header);

            if (result == FFX_API_RETURN_OK)
                _isActive = false;
        }
        else
        {
            _isActive = false;
        }

        LOG_INFO("VK_Configure Enabled: false, result: {} ({})", magic_enum::enum_name((FfxApiReturnCodes) result),
                 (UINT) result);
    }
}

void FSRFG_Vk::EvaluateState(VkDevice device, FG_Constants& fgConstants)
{
    LOG_FUNC();

    _constants = fgConstants;

    if (!FfxApiProxy::IsFGReady())
        FfxApiProxy::InitFfxVk();

    // If needed hooks are missing or XeFG proxy is not inited or FG swapchain is not created
    if (!FfxApiProxy::IsFGReady() || State::Instance().currentVkFGSwapchain == nullptr)
        return;

    if (State::Instance().isShuttingDown)
    {
        DestroyFGContext();
        return;
    }

    static bool lastInfiniteDepth = false;
    bool currentInfiniteDepth = static_cast<bool>(fgConstants.flags & FG_Flags::InfiniteDepth);
    if (lastInfiniteDepth != currentInfiniteDepth)
    {
        lastInfiniteDepth = currentInfiniteDepth;
        LOG_DEBUG("Infinite Depth changed: {}", currentInfiniteDepth);

        State::Instance().FGchanged = true;
        State::Instance().SCchanged = true;
    }

    if (_maxRenderWidth != 0 && _maxRenderHeight != 0 && IsActive() && !IsPaused() &&
        (fgConstants.displayWidth > _maxRenderWidth || fgConstants.displayHeight > _maxRenderHeight))

    {
        State::Instance().FGchanged = true;
        State::Instance().SCchanged = true;
    }

    // If FG Enabled from menu
    if (Config::Instance()->FGEnabled.value_or_default())
    {
        // If FG context is nullptr
        if (_fgContext == nullptr)
        {
            // Create it again
            CreateContext(device, _physicalDevice, fgConstants);

            // Pause for 10 frames
            UpdateTarget();
        }
        // If there is a change deactivate it
        else if (State::Instance().FGchanged)
        {
            Deactivate();

            // Pause for 10 frames
            UpdateTarget();

            // If Swapchain has a change destroy FG Context too
            if (State::Instance().SCchanged)
                DestroyFGContext();
        }

        if (_fgContext != nullptr && State::Instance().activeFgInput == FGInput::Upscaler && !IsPaused() && !IsActive())
            Activate();
    }
    else if (IsActive())
    {
        Deactivate();

        State::Instance().ClearCapturedHudlesses = true;
    }

    if (State::Instance().FGchanged)
    {
        LOG_DEBUG("FGchanged");

        State::Instance().FGchanged = false;

        // Pause for 10 frames
        UpdateTarget();

        // Release FG mutex
        if (Mutex.getOwner() == 2)
            Mutex.unlockThis(2);
    }

    State::Instance().SCchanged = false;
}

void FSRFG_Vk::ReleaseObjects()
{
    LOG_DEBUG("");

    for (size_t i = 0; i < BUFFER_COUNT; i++)
    {
        if (_fgCommandPool[i] != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(_device, _fgCommandPool[i], nullptr);
            _fgCommandPool[i] = VK_NULL_HANDLE;
        }

        if (_fgCommandList[i] != VK_NULL_HANDLE)
        {
            // Command buffers are freed when the pool is destroyed
            _fgCommandList[i] = VK_NULL_HANDLE;
        }
    }

    _mvFlip.reset();
    _depthFlip.reset();
}

bool FSRFG_Vk::ExecuteCommandList(int index)
{
    if (_waitingExecute[index])
    {
        LOG_DEBUG("Executing FG cmdList: {:X}", (size_t) _fgCommandList[index]);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &_fgCommandList[index];

        vkQueueSubmit(_gameQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_gameQueue);

        SetExecuted(index);
    }

    return true;
}

bool FSRFG_Vk::SetResource(VkResource* inputResource)
{
    if (inputResource == nullptr || inputResource->image == VK_NULL_HANDLE || !IsActive() || IsPaused())
        return false;

    // For late sent SL resources
    // we use provided frame index
    auto fIndex = inputResource->frameIndex;
    if (fIndex < 0)
        fIndex = GetIndex();

    auto& type = inputResource->type;

    if (_frameResources[fIndex].contains(type) &&
        _frameResources[fIndex][type].validity == FG_ResourceValidity::ValidNow)
    {
        return false;
    }

    if (type == FG_ResourceType::HudlessColor)
    {
        if (Config::Instance()->FGDisableHudless.value_or_default())
            return false;

        if (!_noHudless[fIndex] && Config::Instance()->FGOnlyAcceptFirstHudless.value_or_default() &&
            inputResource->validity != FG_ResourceValidity::UntilPresentFromDispatch)
        {
            return false;
        }
    }

    if (type == FG_ResourceType::UIColor && Config::Instance()->FGDisableUI.value_or_default())
        return false;

    std::lock_guard<std::mutex> lock(_frMutex);

    if (inputResource->cmdBuffer == VK_NULL_HANDLE && inputResource->validity == FG_ResourceValidity::ValidNow)
    {
        LOG_ERROR("{}, validity == ValidNow but cmdBuffer is nullptr!", magic_enum::enum_name(type));
        return false;
    }

    _frameResources[fIndex][type] = {};
    auto fResource = &_frameResources[fIndex][type];
    fResource->type = type;
    fResource->state = inputResource->state;
    fResource->validity = inputResource->validity;
    fResource->image = inputResource->image;
    fResource->width = inputResource->width;
    fResource->height = inputResource->height;
    fResource->cmdBuffer = inputResource->cmdBuffer;

    auto willFlip = State::Instance().activeFgInput == FGInput::Upscaler &&
                    Config::Instance()->FGResourceFlip.value_or_default() &&
                    (fResource->type == FG_ResourceType::Velocity || fResource->type == FG_ResourceType::Depth);

    // Resource flipping
    if (willFlip && _device != VK_NULL_HANDLE)
    {
        FlipResource(fResource);
    }

    if (type == FG_ResourceType::UIColor)
    {
        // For Vulkan, use the swapchain surface format if available
        // Since VkImage doesn't have GetDesc(), we use a default format or track it elsewhere
        VkFormat format = VK_FORMAT_B8G8R8A8_UNORM; // Default fallback format

        auto uiFormat = (FfxApiSurfaceFormat) ffxApiGetSurfaceFormatVK(format);
        auto scFormat = (FfxApiSurfaceFormat) ffxApiGetSurfaceFormatVK(format);

        if (uiFormat == -1 || scFormat == -1 || uiFormat != scFormat)
        {
            if (!UIFormatTransfer(fIndex, _device, GetUICommandList(fIndex), format, fResource))
            {
                LOG_WARN("Skipping UI resource due to format mismatch! UI: {}, swapchain: {}",
                         (uint32_t)uiFormat, (uint32_t)scFormat);

                _frameResources[fIndex][type] = {};
                return false;
            }
            else
            {
                fResource->validity = FG_ResourceValidity::UntilPresent;
            }
        }

        _noUi[fIndex] = false;
    }
    else if (type == FG_ResourceType::Distortion)
    {
        _noDistortionField[fIndex] = false;
    }
    else if (type == FG_ResourceType::HudlessColor)
    {
        // For Vulkan, use a default format or track it in the resource
        VkFormat scFormat = VK_FORMAT_B8G8R8A8_UNORM; // Default fallback
        auto scFfxFormat = (FfxApiSurfaceFormat) ffxApiGetSurfaceFormatVK(scFormat);

        // TODO: Get actual format from resource or swapchain
        _lastHudlessFormat = scFfxFormat;

        if (_lastHudlessFormat != FFX_API_SURFACE_FORMAT_UNKNOWN && !FormatsCompatible(scFormat, scFormat))
        {
            if (!HudlessFormatTransfer(fIndex, _device, scFormat, fResource))
            {
                LOG_WARN("Skipping hudless resource due to format mismatch! hudless: {}, swapchain: {}",
                         (uint32_t)_lastHudlessFormat, (uint32_t)scFfxFormat);

                _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
                _frameResources[fIndex][type] = {};
                return false;
            }
            else
            {
                fResource->validity = FG_ResourceValidity::UntilPresent;
            }
        }

        _noHudless[fIndex] = false;
    }

    // For FSR FG we always copy ValidNow
    if (fResource->validity == FG_ResourceValidity::ValidButMakeCopy)
        fResource->validity = FG_ResourceValidity::ValidNow;

    fResource->validity = (fResource->validity != FG_ResourceValidity::ValidNow || willFlip)
                              ? FG_ResourceValidity::UntilPresent
                              : FG_ResourceValidity::ValidNow;

    // Copy ValidNow
    if (fResource->validity == FG_ResourceValidity::ValidNow)
    {
        VkImage copyOutput = VK_NULL_HANDLE;

        if (_resourceCopy[fIndex].contains(type))
            copyOutput = _resourceCopy[fIndex].at(type);

        if (!CopyResource(inputResource->cmdBuffer, inputResource->image, &copyOutput, inputResource->state))
        {
            LOG_ERROR("{}, CopyResource error!", magic_enum::enum_name(type));
            return false;
        }

        _resourceCopy[fIndex][type] = copyOutput;
        fResource->copy = copyOutput;
        fResource->state = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        LOG_TRACE("Made a copy: {:X} of input: {:X}", (size_t) fResource->copy, (size_t) fResource->image);
    }

    SetResourceReady(type, fIndex);

    LOG_TRACE("_frameResources[{}][{}]: {:X}", fIndex, magic_enum::enum_name(type), (size_t) fResource->GetResource());
    return true;
}

void FSRFG_Vk::SetCommandQueue(FG_ResourceType type, VkQueue queue, uint32_t queueFamilyIndex)
{
    _gameQueue = queue;
    _gameQueueFamilyIndex = queueFamilyIndex;
}

void FSRFG_Vk::CreateObjects(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice)
{
    _device = InDevice;
    _physicalDevice = InPhysicalDevice;

    if (_fgCommandPool[0] != VK_NULL_HANDLE)
        return;

    LOG_DEBUG("");

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
        if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_fgCommandPool[i]) != VK_SUCCESS)
        {
            LOG_ERROR("CreateCommandPool _fgCommandPool[{}]: failed", i);
            continue;
        }

        allocInfo.commandPool = _fgCommandPool[i];

        if (vkAllocateCommandBuffers(_device, &allocInfo, &_fgCommandList[i]) != VK_SUCCESS)
        {
            LOG_ERROR("AllocateCommandBuffer _fgCommandList[{}]: failed", i);
            continue;
        }
    }
}

bool FSRFG_Vk::Present()
{
    auto fIndex = GetIndexWillBeDispatched();

    if (IsActive() && !IsPaused() && State::Instance().FGHudlessCompare)
    {
        auto hudless = GetResource(FG_ResourceType::HudlessColor, fIndex);
        if (hudless != nullptr)
        {
            if (_hudlessCompare.get() == nullptr)
            {
                _hudlessCompare = std::make_unique<HC_Vk>("HudlessCompare", _device, _physicalDevice);
            }
            else
            {
                if (_hudlessCompare->IsInit())
                {
                    auto commandList = GetUICommandList(fIndex);

                    _hudlessCompare->Dispatch(_device, commandList, hudless->GetResource(),
                                              hudless->state);
                }
            }
        }
    }

    bool result = false;

    // if (IsActive() && !IsPaused())
    {
        if (_uiCommandListResetted[fIndex])
        {
            LOG_DEBUG("Executing _uiCommandList[{}]: {:X}", fIndex, (size_t) _uiCommandList[fIndex]);
            vkEndCommandBuffer(_uiCommandList[fIndex]);

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &_uiCommandList[fIndex];

            vkQueueSubmit(_gameQueue, 1, &submitInfo, VK_NULL_HANDLE);
            vkQueueWaitIdle(_gameQueue);

            _uiCommandListResetted[fIndex] = false;
        }
    }

    if ((_fgFramePresentId - _lastFGFramePresentId) > 3 && IsActive() && !_waitingNewFrameData)
    {
        LOG_DEBUG("Pausing FG");
        Deactivate();
        _waitingNewFrameData = true;
        return false;
    }

    _fgFramePresentId++;

    return Dispatch();
}
