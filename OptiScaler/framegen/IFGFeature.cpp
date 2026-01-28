#include "IFGFeature.h"

#include <Config.h>

int IFGFeature::GetIndex() { return (_frameCount % BUFFER_COUNT); }

int IFGFeature::GetIndexWillBeDispatched()
{
    UINT64 df;

    auto diff = _frameCount - _lastDispatchedFrame;
    auto allowedAhead = Config::Instance()->FGAllowedFrameAhead.value_or_default();

    if (diff > allowedAhead || diff < 0 || _lastDispatchedFrame == 0)
    {
        // If current index has resources, skip to it
        if (HasResource(FG_ResourceType::Depth))
        {
            LOG_DEBUG("Skipping not presented frames! _frameCount: {}, _lastDispatchedFrame: {}", _frameCount,
                      _lastDispatchedFrame);

            df = _frameCount; // Set dispatch frame as new one
        }
        else if (diff > allowedAhead * 2)
        {
            // Large jump without resources - catch up gradually
            df = _lastDispatchedFrame + (diff / 2);
            LOG_DEBUG("Large frame jump in GetIndexWillBeDispatched, catching up to frame {}", df);
        }
        else
        {
            df = _lastDispatchedFrame + 1; // Render next one
        }
    }
    else
    {
        df = _lastDispatchedFrame + 1; // Render next one
    }

    return (df % BUFFER_COUNT);
}

UINT64 IFGFeature::StartNewFrame()
{
    _frameCount++;

    // Adaptive frame jump detection based on FG activity
    // When FG is not active, we expect larger jumps since dispatch isn't happening
    // When FG is active, we need tighter synchronization
    const uint64_t ACTIVE_THRESHOLD = 20;      // Normal threshold when FG is active
    const uint64_t INACTIVE_THRESHOLD = 100;   // Larger threshold when FG is not active
    const uint64_t MODERATE_THRESHOLD = 10;    // Debug logging threshold

    uint64_t threshold = (_isActive || _waitingNewFrameData) ? ACTIVE_THRESHOLD : INACTIVE_THRESHOLD;

    // Only warn about frame jumps if we have a valid last dispatched frame
    // Only check when FG is active - when inactive, _lastDispatchedFrame won't be updated
    if (_isActive && _lastDispatchedFrame > 0 && (_frameCount - _lastDispatchedFrame) > threshold)
    {
        LOG_WARN("Frame count jumped too much! _frameCount: {}, _lastDispatchedFrame: {}",
                 _frameCount, _lastDispatchedFrame);

        // Reset the last dispatched frame to prevent cascading warnings
        // Keep some history to avoid immediate re-triggering
        _lastDispatchedFrame = _frameCount > 5 ? _frameCount - 5 : _frameCount - 1;
    }
    else if (_lastDispatchedFrame == 0)
    {
        // First frame, initialize properly
        _lastDispatchedFrame = _frameCount - 1;
    }
    else if (_isActive && _lastDispatchedFrame > 0 && (_frameCount - _lastDispatchedFrame) > MODERATE_THRESHOLD)
    {
        // Only log moderate jumps when FG is active
        // When FG is not active, _lastDispatchedFrame won't be updated, so this is expected
        LOG_DEBUG("Frame count jumped moderately! _frameCount: {}, _lastDispatchedFrame: {}",
                  _frameCount, _lastDispatchedFrame);

        // Progressive reset: if we're consistently behind, catch up gradually
        if (_frameCount - _lastDispatchedFrame > 15)
        {
            // Catch up by half the difference to avoid sudden jumps
            uint64_t diff = _frameCount - _lastDispatchedFrame;
            _lastDispatchedFrame += diff / 2;
        }
    }

    auto fIndex = GetIndex();
    LOG_DEBUG("_frameCount: {}, fIndex: {}", _frameCount, fIndex);

    _resourceReady[fIndex].clear();
    _waitingExecute[fIndex] = false;

    _noUi[fIndex] = true;
    _noDistortionField[fIndex] = true;
    _noHudless[fIndex] = true;

    NewFrame();

    return _frameCount;
}

bool IFGFeature::IsResourceReady(FG_ResourceType type, int index)
{
    if (index < 0)
        index = GetIndex();

    return _resourceReady[index].contains(type);
}

bool IFGFeature::WaitingExecution(int index)
{
    if (index < 0)
        index = GetIndex();

    return _waitingExecute[index];
}
void IFGFeature::SetExecuted(int index)
{
    if (index < 0)
        index = GetIndex();

    _waitingExecute[index] = false;
}

bool IFGFeature::IsUsingUI() { return !_noUi[GetIndex()]; }
bool IFGFeature::IsUsingUIAny()
{
    for (const auto& value : _noUi)
        if (value == false)
            return true;

    return false;
}
bool IFGFeature::IsUsingDistortionField() { return !_noDistortionField[GetIndex()]; }
bool IFGFeature::IsUsingHudless(int index)
{
    if (index < 0)
        index = GetIndex();

    return !_noHudless[index];
}

bool IFGFeature::IsUsingHudlessAny()
{
    for (const auto& value : _noHudless)
        if (value == false)
            return true;

    return false;
}

bool IFGFeature::CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject)
{
    if (streamlineRiid.Data1 == 0)
    {
        auto iidResult = IIDFromString(L"{ADEC44E2-61F0-45C3-AD9F-1B37379284FF}", &streamlineRiid);

        if (iidResult != S_OK)
            return false;
    }

    auto qResult = pObject->QueryInterface(streamlineRiid, (void**) ppRealObject);

    if (qResult == S_OK && *ppRealObject != nullptr)
    {
        LOG_INFO("{} Streamline proxy found!", functionName);
        (*ppRealObject)->Release();
        return true;
    }

    return false;
}

int IFGFeature::GetDispatchIndex(UINT64& willDispatchFrame)
{
    LOG_DEBUG("_lastDispatchedFrame: {},  _frameCount: {}", _lastDispatchedFrame, _frameCount);

    // We are in same frame
    if (_frameCount == _lastDispatchedFrame)
        return -1;

    auto diff = _frameCount - _lastDispatchedFrame;
    auto allowedAhead = Config::Instance()->FGAllowedFrameAhead.value_or_default();

    // Handle frame jumps more gracefully
    if (diff > allowedAhead || diff < 0 || _lastDispatchedFrame == 0)
    {
        LOG_DEBUG("Frame jump detected! diff: {}, allowed: {}", diff, allowedAhead);

        if (HasResource(FG_ResourceType::Depth))
        {
            // Have resources for current frame, dispatch it
            willDispatchFrame = _frameCount;
        }
        else if (diff > allowedAhead * 2)
        {
            // Large jump without resources - catch up gradually
            // This prevents the "jump too much" warning from triggering
            willDispatchFrame = _lastDispatchedFrame + (diff / 2);
            LOG_DEBUG("Large frame jump, catching up gradually to frame {}", willDispatchFrame);
        }
        else
        {
            // Normal case - just render next frame
            willDispatchFrame = _lastDispatchedFrame + 1;
        }
    }
    else
    {
        willDispatchFrame = _lastDispatchedFrame + 1; // Render next one
    }

    _lastDispatchedFrame = willDispatchFrame;
    _lastFGFrame = State::Instance().FGLastFrame;

    return (willDispatchFrame % BUFFER_COUNT);
}

bool IFGFeature::IsActive() { return _isActive || _waitingNewFrameData; }

bool IFGFeature::IsPaused() { return _targetFrame != 0 && _targetFrame >= _frameCount; }

bool IFGFeature::IsDispatched() { return _lastDispatchedFrame == _frameCount; }

bool IFGFeature::IsLowResMV() { return !_constants.flags[FG_Flags::DisplayResolutionMVs]; }

bool IFGFeature::IsAsync() { return _constants.flags[FG_Flags::Async]; }

bool IFGFeature::IsHdr() { return _constants.flags[FG_Flags::Hdr]; }

bool IFGFeature::IsJitteredMVs() { return _constants.flags[FG_Flags::JitteredMVs]; }

bool IFGFeature::IsInvertedDepth() { return _constants.flags[FG_Flags::InvertedDepth]; }

bool IFGFeature::IsInfiniteDepth() { return _constants.flags[FG_Flags::InfiniteDepth]; }

void IFGFeature::SetJitter(float x, float y, int index)
{
    if (index < 0)
        index = GetIndex();

    _jitterX[index] = x;
    _jitterY[index] = y;
}

void IFGFeature::SetMVScale(float x, float y, int index)
{
    if (index < 0)
        index = GetIndex();

    _mvScaleX[index] = x;
    _mvScaleY[index] = y;
}

void IFGFeature::SetCameraValues(float nearValue, float farValue, float vFov, float aspectRatio, float meterFactor,
                                 int index)
{
    if (index < 0)
        index = GetIndex();

    _cameraFar[index] = farValue;
    _cameraNear[index] = nearValue;
    _cameraVFov[index] = vFov;
    _cameraAspectRatio[index] = aspectRatio;
    _meterFactor[index] = meterFactor;
}

void IFGFeature::SetCameraData(float cameraPosition[3], float cameraUp[3], float cameraRight[3], float cameraForward[3],
                               int index)
{
    if (index < 0)
        index = GetIndex();

    std::memcpy(_cameraPosition[index], cameraPosition, 3 * sizeof(float));
    std::memcpy(_cameraUp[index], cameraUp, 3 * sizeof(float));
    std::memcpy(_cameraRight[index], cameraRight, 3 * sizeof(float));
    std::memcpy(_cameraForward[index], cameraForward, 3 * sizeof(float));
}

void IFGFeature::SetFrameTimeDelta(double delta, int index)
{
    if (index < 0)
        index = GetIndex();

    _ftDelta[index] = delta;
}

void IFGFeature::SetReset(UINT reset, int index)
{
    if (index < 0)
        index = GetIndex();

    _reset[index] = reset;
}

void IFGFeature::SetInterpolationRect(UINT64 width, UINT height, int index)
{
    if (index < 0)
        index = GetIndex();

    _interpolationWidth[index] = width;
    _interpolationHeight[index] = height;
}

void IFGFeature::GetInterpolationRect(UINT64& width, UINT& height, int index)
{
    if (index < 0)
        index = GetIndex();

    width = _interpolationWidth[index];
    height = _interpolationHeight[index];
}

void IFGFeature::SetInterpolationPos(UINT left, UINT top, int index)
{
    if (index < 0)
        index = GetIndex();

    _interpolationLeft[index] = left;
    _interpolationTop[index] = top;
}

void IFGFeature::GetInterpolationPos(UINT& left, UINT& top, int index)
{
    if (index < 0)
        index = GetIndex();

    if (_interpolationLeft[index].has_value())
        left = _interpolationLeft[index].value();
    else
        left = 0;

    if (_interpolationTop[index].has_value())
        top = _interpolationTop[index].value();
    else
        top = 0;
}

void IFGFeature::ResetCounters() { _targetFrame = _frameCount; }

void IFGFeature::UpdateTarget()
{
    _targetFrame = _frameCount + 10;
    //_lastDispatchedFrame = 0;
    LOG_DEBUG("Current frame: {} target frame: {}", _frameCount, _targetFrame);
}

UINT64 IFGFeature::FrameCount() { return _frameCount; }

UINT64 IFGFeature::LastDispatchedFrame() { return _lastDispatchedFrame; }

UINT64 IFGFeature::TargetFrame() { return _targetFrame; }

void IFGFeature::SetResourceReady(FG_ResourceType type, int index)
{
    if (index < 0)
        index = GetIndex();

    _resourceReady[index][type] = true;
    _resourceFrame[type] = _frameCount;
}
