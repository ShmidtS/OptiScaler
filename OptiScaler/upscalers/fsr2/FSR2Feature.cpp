#include <pch.h>
#include "FSR2Feature.h"

#include <State.h>

double FSR2Feature::GetDeltaTime()
{
    double currentTime = Util::MillisecondsNow();
    double deltaTime = (currentTime - _lastFrameTime);
    _lastFrameTime = currentTime;
    return deltaTime;
}

FSR2Feature::~FSR2Feature()
{
    if (!IsInited())
        return;

    // Always destroy context and free memory, even during shutdown
    // to prevent memory leaks
    auto errorCode = ffxFsr2ContextDestroy(&_context);
    free(_contextDesc.callbacks.scratchBuffer);

    SetInit(false);
}
