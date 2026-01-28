#include "Streamline_Inputs_Vk.h"

#include <Config.h>

#include <resource_tracking/ResTrack_vk.h>

#include <magic_enum.hpp>

void Sl_Inputs_Vk::CheckForFrame(IFGFeature_Vk* fg, uint32_t frameId)
{
    std::scoped_lock lock(_frameBoundaryMutex);

    if (_isFrameFinished && _lastFrameId == _currentFrameId && frameId == 0 && frameId != _currentFrameId)
    {
        LOG_DEBUG("1> CheckForFrame: frameId={}, currentFrameId={}, lastFrameId={}, isFrameFinished={}", frameId,
                  _currentFrameId, _lastFrameId, _isFrameFinished);

        _isFrameFinished = false;

        fg->StartNewFrame();
        _currentIndex = fg->GetIndex();

        if (frameId != 0)
            _currentFrameId = frameId;
        else
            _currentFrameId = _lastFrameId + 1;

        _frameIdIndex[_currentIndex] = _currentFrameId;
    }
    else if (frameId != 0 && frameId > _currentFrameId)
    {
        LOG_DEBUG("2> CheckForFrame: frameId={}, currentFrameId={}, lastFrameId={}, isFrameFinished={}", frameId,
                  _currentFrameId, _lastFrameId, _isFrameFinished);

        _isFrameFinished = false;
        _lastFrameId = frameId - 1;

        fg->StartNewFrame();
        _currentIndex = fg->GetIndex();
        _currentFrameId = frameId;
        _frameIdIndex[_currentIndex] = _currentFrameId;
    }
}

int Sl_Inputs_Vk::IndexForFrameId(uint32_t frameId) const
{
    for (int i = 0; i < BUFFER_COUNT; i++)
    {
        if (_frameIdIndex[i] == frameId)
            return i;
    }

    return -1;
}

bool Sl_Inputs_Vk::setConstants(const sl::Constants& values, uint32_t frameId)
{
    auto fgOutput = State::Instance().currentVkFG;

    if (fgOutput == nullptr)
    {
        LOG_WARN("currentVkFG is nullptr - Vulkan frame generation not initialized");
        return false;
    }

    LOG_DEBUG("frameId: {}, currentVkFG: {:X}", frameId, (size_t) fgOutput);
    CheckForFrame(fgOutput, frameId);

    auto data = sl::Constants {};
    bool dataFound = false;

    if (values.structVersion == data.structVersion)
    {
        data = values;
        dataFound = true;
    }
    else if ((data.structVersion == sl::kStructVersion2 && values.structVersion == sl::kStructVersion1) ||
             values.structVersion == 0)
    // Spider-Man Remastered does this funny thing of sending an invalid struct version
    {
        auto* pNext = data.next;
        memcpy(&data, &values, sizeof(values) - sizeof(sl::Constants::minRelativeLinearDepthObjectSeparation));
        data.structVersion = sl::kStructVersion2;
        data.next = pNext;

        dataFound = true;
    }

    if (dataFound)
    {
        auto config = Config::Instance();

        // FG Evaluate part
        FG_Constants fgConstants {};

        // TODO
        fgConstants.displayWidth = 0;
        fgConstants.displayHeight = 0;

        fgConstants.flags.reset();

        if (data.depthInverted)
            fgConstants.flags |= FG_Flags::InvertedDepth;

        if (data.motionVectorsJittered)
            fgConstants.flags |= FG_Flags::JitteredMVs;

        if (data.motionVectorsDilated)
            fgConstants.flags |= FG_Flags::DisplayResolutionMVs;

        if (config->FGAsync.value_or_default())
            fgConstants.flags |= FG_Flags::Async;

        if (infiniteDepth)
            fgConstants.flags |= FG_Flags::InfiniteDepth;

        if (config->FGXeFGDepthInverted.value_or_default() != (data.depthInverted == sl::Boolean::eTrue) ||
            config->FGXeFGJitteredMV.value_or_default() != (data.motionVectorsJittered == sl::Boolean::eTrue) ||
            config->FGXeFGHighResMV.value_or_default() != (data.motionVectorsDilated == sl::Boolean::eTrue))
        {
            config->FGXeFGDepthInverted = (data.depthInverted == sl::Boolean::eTrue);
            config->FGXeFGJitteredMV = (data.motionVectorsJittered == sl::Boolean::eTrue);
            config->FGXeFGHighResMV = (data.motionVectorsDilated == sl::Boolean::eTrue);
            LOG_DEBUG("XeFG DepthInverted: {}", config->FGXeFGDepthInverted.value_or_default());
            LOG_DEBUG("XeFG JitteredMV: {}", config->FGXeFGJitteredMV.value_or_default());
            LOG_DEBUG("XeFG HighResMV: {}", config->FGXeFGHighResMV.value_or_default());
            config->SaveXeFG();
        }

        fgOutput->EvaluateState(State::Instance().currentVkDevice, fgConstants);

        if (!config->FGEnabled.value_or_default())
        {
            LOG_TRACE("FG not active or paused");
            return true;
        }
        else
        {
            if (!fgOutput->IsActive() && !fgOutput->IsPaused())
            {
                fgOutput->Activate();
            }
            else if (!fgOutput->IsActive() || fgOutput->IsPaused())
            {
                LOG_TRACE("FG not active or paused");
                return true;
            }
        }

        // Frame data part

        // Nukem's function, licensed under GPLv3
        auto loadCameraMatrix = [&]()
        {
            if (data.orthographicProjection)
                return false;

            float projMatrix[4][4];
            memcpy(projMatrix, (void*) &data.cameraViewToClip, sizeof(projMatrix));

            // BUG: Various RTX Remix-based games pass in an identity matrix which is completely useless. No
            // idea why.
            const bool isEmptyOrIdentityMatrix = [&]()
            {
                float m[4][4] = {};
                if (memcmp(projMatrix, m, sizeof(m)) == 0)
                    return true;

                m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
                return memcmp(projMatrix, m, sizeof(m)) == 0;
            }();

            if (isEmptyOrIdentityMatrix)
                return false;

            // a 0 0 0
            // 0 b 0 0
            // 0 0 c e
            // 0 0 d 0
            const double b = projMatrix[1][1];
            const double c = projMatrix[2][2];
            const double d = projMatrix[3][2];
            const double e = projMatrix[2][3];

            if (e < 0.0)
            {
                data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (d / c));
                data.cameraFar = static_cast<float>(d / (c + 1.0));
            }
            else
            {
                data.cameraNear = static_cast<float>((c == 0.0) ? 0.0 : (-d / c));
                data.cameraFar = static_cast<float>(-d / (c - 1.0));
            }

            if (data.depthInverted)
                std::swap(data.cameraNear, data.cameraFar);

            data.cameraFOV = static_cast<float>(2.0 * std::atan(1.0 / b));
            return true;
        };

        static bool dontRecalc = false;

        LOG_TRACE("Camera from SL pre recalc near: {}, far: {}", data.cameraNear, data.cameraFar);

        // UE seems to not be passing the correct cameraViewToClip
        // and we can't use it to calculate cameraNear and cameraFar.
        if (engineType != sl::EngineType::eUnreal && !dontRecalc)
            loadCameraMatrix();

        // Workaround for more games with broken cameraViewToClip
        if (!dontRecalc && (data.cameraNear < 0.0f || data.cameraFar < 0.0f))
            dontRecalc = true;

        infiniteDepth = false;
        if (data.cameraNear != 0.0f && data.cameraFar == 0.0f)
        {
            // A CameraFar value of zero indicates an infinite far plane. Due to a bug in FSR's
            // setupDeviceDepthToViewSpaceDepthParams function, CameraFar must always be greater than
            // CameraNear when in use.

            infiniteDepth = true;
            data.cameraFar = data.cameraNear + 1.0f;
        }

        fgOutput->SetCameraValues(data.cameraNear, data.cameraFar, data.cameraFOV, data.cameraAspectRatio, 0.0f);

        fgOutput->SetJitter(data.jitterOffset.x, data.jitterOffset.y);

        // Streamline is not 100% clear on if we should multiply by resolution or not.
        // But UE games and Dead Rising expect that multiplication to be done, even if the scale is 1.0.
        // bool multiplyByResolution = dataCopy.mvecScale.x != 1.f || dataCopy.mvecScale.y != 1.f;
        bool multiplyByResolution = true;
        if (multiplyByResolution)
            fgOutput->SetMVScale(data.mvecScale.x * mvsWidth, data.mvecScale.y * mvsHeight);
        else
            fgOutput->SetMVScale(data.mvecScale.x, data.mvecScale.y);

        fgOutput->SetCameraData(reinterpret_cast<float*>(&data.cameraPos), reinterpret_cast<float*>(&data.cameraUp),
                                reinterpret_cast<float*>(&data.cameraRight), reinterpret_cast<float*>(&data.cameraFwd));

        fgOutput->SetReset(data.reset == sl::Boolean::eTrue);

        fgOutput->SetFrameTimeDelta(static_cast<float>(State::Instance().lastFGFrameTime));
    }
    else
    {
        LOG_ERROR("Wrong constant struct version");
    }

    return dataFound;
}

bool Sl_Inputs_Vk::evaluateState(VkDevice device)
{
    auto fgOutput = State::Instance().currentVkFG;

    if (fgOutput == nullptr)
    {
        LOG_WARN("currentVkFG is nullptr - Vulkan frame generation not initialized");
        return false;
    }

    LOG_DEBUG("currentVkFG: {:X}, IsActive: {}, IsPaused: {}", (size_t) fgOutput, fgOutput->IsActive(), fgOutput->IsPaused());

    static UINT64 lastFrameCount = 0;
    static UINT64 repeatsInRow = 0;
    if (lastFrameCount == fgOutput->FrameCount())
    {
        repeatsInRow++;
    }
    else
    {
        lastFrameCount = fgOutput->FrameCount();
        repeatsInRow = 0;
    }

    if (repeatsInRow > 10 && fgOutput->IsActive())
    {
        LOG_WARN("Many frame count repeats in a row, stopping FG");
        State::Instance().FGchanged = true;
        repeatsInRow = 0;
        return false;
    }

    return true;
}

bool Sl_Inputs_Vk::reportResource(const sl::ResourceTag& tag, VkCommandBuffer cmdBuffer, uint32_t frameId)
{
    auto& state = State::Instance();
    state.DLSSGLastFrame = state.FGLastFrame;

    auto fgOutput = state.currentVkFG;

    // It's possible for only some resources to be marked ready if FGEnabled is enabled during resource tagging
    if (fgOutput == nullptr)
    {
        LOG_WARN("currentVkFG is nullptr - Vulkan frame generation not initialized");
        return false;
    }

    if (!Config::Instance()->FGEnabled.value_or_default())
        return false;

    LOG_DEBUG("Reporting SL resource type: {} lifecycle: {} frameId: {}, currentVkFG: {:X}", tag.type,
              magic_enum::enum_name(tag.lifecycle), frameId, (size_t) fgOutput);

    CheckForFrame(fgOutput, frameId);

    if (tag.resource->native == nullptr)
    {
        LOG_TRACE("tag.resource->native is null");
        return false;
    }

    if (!cmdBuffer && tag.lifecycle == sl::eOnlyValidNow)
        LOG_TRACE("cmdBuffer is null");

    auto vkResourceHandle = reinterpret_cast<NGXVulkanResourceHandle*>(tag.resource->native);

    // Validate Vulkan resource handle
    if (!vkResourceHandle || vkResourceHandle->Type != 0) // Type 0 = Image, Type 1 = Buffer
    {
        LOG_TRACE("Invalid Vulkan resource handle");
        return false;
    }

    VkImage vkImage = vkResourceHandle->ImageMetadata.Image;
    uint32_t width = vkResourceHandle->ImageMetadata.Width;
    uint32_t height = vkResourceHandle->ImageMetadata.Height;
    VkFormat format = vkResourceHandle->ImageMetadata.Format;

    VkResource res = {};
    res.image = vkImage;
    res.cmdBuffer = cmdBuffer; // Critical for eOnlyValidNow
    res.width = tag.extent ? tag.extent.width : width;
    res.height = tag.extent ? tag.extent.height : height;
    res.state = (VkImageLayout)tag.resource->state; // Using state as layout
    res.validity =
        (tag.lifecycle == sl::eOnlyValidNow) ? FG_ResourceValidity::ValidNow : FG_ResourceValidity::UntilPresent;

    // Populate createInfo for FFX API
    res.createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    res.createInfo.imageType = VK_IMAGE_TYPE_2D;
    res.createInfo.format = format;
    res.createInfo.extent = { static_cast<uint32_t>(res.width), res.height, 1 };
    res.createInfo.mipLevels = 1;
    res.createInfo.arrayLayers = 1;
    res.createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    res.createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    res.createInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    res.createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Store queue info if available
    if (_gameQueue != VK_NULL_HANDLE)
    {
        State::Instance().currentVkDevice = State::Instance().currentVkDevice; // Already set elsewhere
    }

    if (frameId > 0)
    {
        int index = IndexForFrameId(frameId);

        if (index >= 0)
        {
            res.frameIndex = index;
        }
        else
        {
            LOG_WARN("Frame ID {} not found in tracking, using current index {}", frameId, _currentIndex);
            res.frameIndex = _currentIndex;
        }
    }
    else
    {
        res.frameIndex = -1;
    }

    bool handled = true;

    // Map types
    if (tag.type == sl::kBufferTypeDepth || tag.type == sl::kBufferTypeHiResDepth ||
        tag.type == sl::kBufferTypeLinearDepth)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Depth, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGDepthValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Depth;
        fgOutput->SetResource(&res);
    }
    else if (tag.type == sl::kBufferTypeMotionVectors)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::Velocity, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        if (Config::Instance()->FGVelocityValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        res.type = FG_ResourceType::Velocity;
        mvsWidth = res.width; // Track locally for dispatch logic
        mvsHeight = res.height;
        fgOutput->SetResource(&res);
    }
    else if (tag.type == sl::kBufferTypeHUDLessColor)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::HudlessColor, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        res.type = FG_ResourceType::HudlessColor;

        if (Config::Instance()->FGHudlessValidNow.value_or_default())
            res.validity = FG_ResourceValidity::ValidNow;

        fgOutput->SetInterpolationRect(res.width, res.height);
        fgOutput->SetResource(&res);
    }
    else if (tag.type == sl::kBufferTypeUIColorAndAlpha)
    {
        if (res.frameIndex < 0)
        {
            res.frameIndex = fgOutput->GetIndexWillBeDispatched();

            if (fgOutput->HasResource(FG_ResourceType::UIColor, res.frameIndex))
                res.frameIndex = fgOutput->GetIndex();
        }

        res.type = FG_ResourceType::UIColor;

        // Fallback size logic
        UINT64 width = 0;
        UINT height = 0;
        fgOutput->GetInterpolationRect(width, height, _currentIndex);

        if (width == 0)
            fgOutput->SetInterpolationRect(res.width, res.height);

        fgOutput->SetResource(&res);
    }
    else
    {
        handled = false;
    }

    return handled;
}

bool Sl_Inputs_Vk::dispatchFG()
{
    LOG_DEBUG();
    return true;
}

void Sl_Inputs_Vk::markPresent(uint64_t frameId)
{
    std::scoped_lock lock(_frameBoundaryMutex);
    LOG_TRACE("frameId: {}", frameId);
    _isFrameFinished = true;
    _lastFrameId = static_cast<uint32_t>(frameId);
}
