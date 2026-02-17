#pragma once
#include <atomic>
#include "SysUtils.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

#include <unordered_set>
#include <Util.h>

#define DLSS_MOD_ID_OFFSET 1000000

inline static std::atomic<unsigned int> handleCounter{DLSS_MOD_ID_OFFSET};

struct InitFlags
{
    bool IsHdr;
    bool SharpenEnabled;
    bool LowResMV;
    bool AutoExposure;
    bool DepthInverted;
    bool JitteredMV;
};

class IFeature
{
  private:
    bool _isInited = false;
    int _featureFlags = 0;
    InitFlags _initFlags = {};

    NVSDK_NGX_PerfQuality_Value _perfQualityValue;

    struct JitterInfo
    {
        float x;
        float y;
    };

    struct hashFunction
    {
        size_t operator()(const std::pair<float, float>& p) const
        {
            size_t h1 = std::hash<float>()(p.first);
            size_t h2 = std::hash<float>()(p.second);
            return h1 ^ (h2 << 1);
        }
    };

    std::unordered_set<std::pair<float, float>, hashFunction> _jitterInfo;

  protected:
    // D3D11with12 - Initialized once at startup, then read-only
    // Thread safety: Written during init (single-threaded), read-only after initialization completes
    // Safe for concurrent read access after initialization
    inline static ID3D12Device* _dx11on12Device = nullptr;
    inline static ID3D12Device* _localDx11on12Device = nullptr;

    bool _initParameters = false;
    std::unique_ptr<NVSDK_NGX_Handle> _handle;

    float _sharpness = 0;
    bool _hasColor = false;
    bool _hasDepth = false;
    bool _hasMV = false;
    bool _hasTM = false;
    bool _accessToReactiveMask = false;
    bool _hasExposure = false;
    bool _hasOutput = false;

    unsigned int _renderWidth = 0;
    unsigned int _renderHeight = 0;
    unsigned int _targetWidth = 0;
    unsigned int _targetHeight = 0;
    unsigned int _displayWidth = 0;
    unsigned int _displayHeight = 0;

    long _frameCount = 0;
    bool _featureFrozen = false;
    bool _moduleLoaded = false;

    void SetHandle(unsigned int InHandleId);
    bool SetInitParameters(NVSDK_NGX_Parameter* InParameters);
    void GetRenderResolution(NVSDK_NGX_Parameter* InParameters, unsigned int* OutWidth, unsigned int* OutHeight);
    void GetDynamicOutputResolution(NVSDK_NGX_Parameter* InParameters, unsigned int* width, unsigned int* height);
    float GetSharpness(const NVSDK_NGX_Parameter* InParameters);

    // Common camera parameters setup - used by FSR2/FSR31/XeSS
    struct CameraParams
    {
        float Near = 0.0f;
        float Far = 0.0f;
        float FovAngleVertical = 1.0471975511966f;
    };

    CameraParams SetupCameraParams(const NVSDK_NGX_Parameter* InParameters);

    // Validate and clamp scaling multiplier between 0.5f and 3.0f
    float GetValidatedScalingMultiplier();

    virtual void SetInit(bool InValue) { _isInited = InValue; }

  public:
    NVSDK_NGX_Handle* Handle() const { return _handle.get(); };
    static unsigned int GetNextHandleId() { return handleCounter++; }
    int GetFeatureFlags() const { return _featureFlags; }

    virtual bool IsWithDx12() = 0;
    virtual feature_version Version() = 0;
    virtual std::string Name() const = 0;

    size_t JitterCount() { return _jitterInfo.size(); }

    void TickFrozenCheck();
    bool IsFrozen() const { return _featureFrozen; };
    bool UpdateOutputResolution(const NVSDK_NGX_Parameter* InParameters);
    unsigned int DisplayWidth() const { return _displayWidth; };
    unsigned int DisplayHeight() const { return _displayHeight; };
    unsigned int TargetWidth() const { return _targetWidth; };
    unsigned int TargetHeight() const { return _targetHeight; };
    unsigned int RenderWidth() const { return _renderWidth; };
    unsigned int RenderHeight() const { return _renderHeight; };
    NVSDK_NGX_PerfQuality_Value PerfQualityValue() const { return _perfQualityValue; }
    bool IsInitParameters() const { return _initParameters; };
    bool IsInited() const { return _isInited; }
    float Sharpness() const { return _sharpness; }
    bool HasColor() const { return _hasColor; }
    bool HasDepth() const { return _hasDepth; }
    bool HasMV() const { return _hasMV; }
    bool HasTM() const { return _hasTM; }
    bool AccessToReactiveMask() const { return _accessToReactiveMask; }
    bool HasExposure() const { return _hasExposure; }
    bool HasOutput() const { return _hasOutput; }
    bool ModuleLoaded() const { return _moduleLoaded; }
    long FrameCount() { return _frameCount; }

    bool AutoExposure() { return _initFlags.AutoExposure; }
    bool DepthInverted() { return _initFlags.DepthInverted; }
    bool IsHdr() { return _initFlags.IsHdr; }
    bool JitteredMV() { return _initFlags.JitteredMV; }
    bool LowResMV() { return _initFlags.LowResMV; }
    bool SharpenEnabled() { return _initFlags.SharpenEnabled; }

    IFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) { SetHandle(InHandleId); }

    virtual ~IFeature() = default;

    // Safe parameter retrieval with nullptr check and error handling
    template<typename T>
    static bool GetParameterSafe(NVSDK_NGX_Parameter* params, const char* name, T& outValue, T defaultValue = T{});
};

// Template specializations for common types
template<>
inline bool IFeature::GetParameterSafe<int>(NVSDK_NGX_Parameter* params, const char* name, int& outValue, int defaultValue)
{
    if (params == nullptr || name == nullptr)
    {
        outValue = defaultValue;
        return false;
    }

    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetI(params, name, &outValue);
    if (result != NVSDK_NGX_Result_Success)
    {
        outValue = defaultValue;
        return false;
    }

    return true;
}

template<>
inline bool IFeature::GetParameterSafe<float>(NVSDK_NGX_Parameter* params, const char* name, float& outValue, float defaultValue)
{
    if (params == nullptr || name == nullptr)
    {
        outValue = defaultValue;
        return false;
    }

    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetF(params, name, &outValue);
    if (result != NVSDK_NGX_Result_Success)
    {
        outValue = defaultValue;
        return false;
    }

    return true;
}

template<>
inline bool IFeature::GetParameterSafe<double>(NVSDK_NGX_Parameter* params, const char* name, double& outValue, double defaultValue)
{
    if (params == nullptr || name == nullptr)
    {
        outValue = defaultValue;
        return false;
    }

    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetD(params, name, &outValue);
    if (result != NVSDK_NGX_Result_Success)
    {
        outValue = defaultValue;
        return false;
    }

    return true;
}

template<>
inline bool IFeature::GetParameterSafe<unsigned int>(NVSDK_NGX_Parameter* params, const char* name, unsigned int& outValue, unsigned int defaultValue)
{
    if (params == nullptr || name == nullptr)
    {
        outValue = defaultValue;
        return false;
    }

    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetUI(params, name, &outValue);
    if (result != NVSDK_NGX_Result_Success)
    {
        outValue = defaultValue;
        return false;
    }

    return true;
}

template<>
inline bool IFeature::GetParameterSafe<UINT64>(NVSDK_NGX_Parameter* params, const char* name, UINT64& outValue, UINT64 defaultValue)
{
    if (params == nullptr || name == nullptr)
    {
        outValue = defaultValue;
        return false;
    }

    unsigned long long value = 0;
    NVSDK_NGX_Result result = NVSDK_NGX_Parameter_GetULL(params, name, &value);
    if (result != NVSDK_NGX_Result_Success)
    {
        outValue = defaultValue;
        return false;
    }

    outValue = static_cast<UINT64>(value);
    return true;
}
