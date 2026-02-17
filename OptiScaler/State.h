// ===========================================================================
// State.h - Global Application State Singleton
// ===========================================================================
// Thread Safety Notes:
// - State is a singleton accessed from multiple threads (main thread, hook threads)
// - Most fields are written during initialization (DLL_PROCESS_ATTACH) and read-only after
// - Fields that may be modified at runtime should use appropriate synchronization:
//   - frameTimes/upscaleTimes: protected by frameTimeMutex
//   - versionCheck*: protected by versionCheckMutex
// - Config access is generally thread-safe via ConfigOptions atomic operations
// - For runtime state changes, prefer using the Scoped* helper classes
// ===========================================================================

#pragma once
#include <atomic>
#include "upscalers/IFeature.h"
#include "framegen/IFGFeature_Dx12.h"
#include "framegen/IFGFeature_Vk.h"
#include <inputs/FG/Streamline_Inputs_Dx12.h>
#include "misc/Quirks.h"

#include <set>
#include <deque>
#include <vulkan/vulkan.h>
#include <ankerl/unordered_dense.h>
#include <mutex>

typedef enum API
{
    NotSelected = 0,
    DX11,
    DX12,
    Vulkan,
} API;

enum class FGPreset : uint32_t
{
    NoFG,
    OptiFG,
    Nukems,
};

enum class FGInput : uint32_t
{
    NoFG,
    Nukems,
    FSRFG,
    DLSSG, // technically Streamline inputs
    XeFG,
    Upscaler, // OptiFG
    FSRFG30,
};

enum class FGOutput : uint32_t
{
    NoFG,
    Nukems,
    FSRFG,
    DLSSG,
    XeFG
};

typedef struct CapturedHudlessInfo
{
    UINT64 usageCount = 1;
    UINT captureInfo = 0;
    bool enabled = true;
} captured_hudless_info;

class State
{
  public:
    // Thread-safe singleton using Meyers' singleton pattern
    // C++11 guarantees static local initialization is thread-safe
    // Note: Individual field access still requires appropriate synchronization
    static State& Instance()
    {
        static State instance;
        return instance;
    }

    std::string GameName;
    std::string GameExe;
    ankerl::unordered_dense::map<void*, std::string> DeviceAdapterNames;

    bool NvngxDx11Inited = false;
    bool NvngxDx12Inited = false;
    bool NvngxVkInited = false;

    flag_set<GameQuirk> gameQuirks;
    bool isOptiPatcherSucceed = false;

    // Reseting on creation of new feature
    std::optional<bool> AutoExposure;

    // FG
    UINT64 FGLastFrame = 0;

    // DLSSG
    bool NukemsFilesAvailable = false;
    bool DLSSGDebugView = false;
    bool DLSSGInterpolatedOnly = false;
    uint32_t delayMenuRenderBy = 0;
    UINT64 DLSSGLastFrame = 0;

    // FSR Common
    float lastFsrCameraNear = 0.0f;
    float lastFsrCameraFar = 0.0f;

    // Frame Generation
    FGInput activeFgInput = FGInput::NoFG;
    FGOutput activeFgOutput = FGOutput::NoFG;

    // Streamline FG inputs
    Sl_Inputs_Dx12 slFGInputs = {};

    // OptiFG
    bool FGPresentIsCalled = false;
    bool FGonlyGenerated = false;
    bool FGHudlessCompare = false;
    bool FGchanged = false;
    bool SCchanged = false;

    bool FGcaptureResources = false;
    size_t FGcapturedResourceCount = 0;
    bool FGresetCapturedResources = false;
    bool FGonlyUseCapturedResources = false;

    bool FSRFGFTPchanged = false;
    bool FSRFGInputActive = false;

    ankerl::unordered_dense::map<void*, CapturedHudlessInfo> CapturedHudlesses;
    bool ClearCapturedHudlesses = false;

    // NVNGX init parameters
    uint64_t NVNGX_ApplicationId = 1337;
    std::wstring NVNGX_ApplicationDataPath;
    std::string NVNGX_ProjectId;
    NVSDK_NGX_Version NVNGX_Version {};
    const NVSDK_NGX_FeatureCommonInfo* NVNGX_FeatureInfo = nullptr;
    std::vector<std::wstring> NVNGX_FeatureInfo_Paths;
    NVSDK_NGX_LoggingInfo NVNGX_Logger { nullptr, NVSDK_NGX_LOGGING_LEVEL_OFF, false };
    NVSDK_NGX_EngineType NVNGX_Engine = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    std::string NVNGX_EngineVersion;
    std::optional<std::wstring> NVNGX_DLSS_Path;
    std::optional<std::wstring> NVNGX_DLSSD_Path;
    std::optional<std::wstring> NVNGX_DLSSG_Path;

    // NGX OTA
    std::string NGX_OTA_Dlss;
    std::string NGX_OTA_Dlssd;

    feature_version streamlineVersion = { 0, 0, 0 };
    bool streamlineLoaded = false;

    API api = API::NotSelected;
    API swapchainApi = API::NotSelected;

    // Framerate
    bool reflexLimitsFps = false;
    bool reflexShowWarning = false;
    bool rtssReflexInjection = false;
    UINT64 frameCount = 0;

    // for realtime changes
    ankerl::unordered_dense::map<unsigned int, bool> changeBackend;
    std::string newBackend = "";

    // XeSS debug stuff
    bool xessDebug = false;
    int xessDebugFrames = 5;
    float lastMipBias = 100.0f;
    float lastMipBiasMax = -100.0f;

    int xefgMaxInterpolationCount = 1;

    // DLSS
    bool dlssPresetsOverriddenExternally = false;
    bool dlssPresetsOverridenByOpti = false;
    uint32_t dlssRenderPresetDLAA = 0;
    uint32_t dlssRenderPresetUltraQuality = 0;
    uint32_t dlssRenderPresetQuality = 0;
    uint32_t dlssRenderPresetBalanced = 0;
    uint32_t dlssRenderPresetPerformance = 0;
    uint32_t dlssRenderPresetUltraPerformance = 0;

    // DLSSD
    bool dlssdPresetsOverriddenExternally = false;
    bool dlssdPresetsOverridenByOpti = false;
    uint32_t dlssdRenderPresetDLAA = 0;
    uint32_t dlssdRenderPresetUltraQuality = 0;
    uint32_t dlssdRenderPresetQuality = 0;
    uint32_t dlssdRenderPresetBalanced = 0;
    uint32_t dlssdRenderPresetPerformance = 0;
    uint32_t dlssdRenderPresetUltraPerformance = 0;

    // Spoofing
    std::atomic<bool> skipSpoofing{false};
    // For DXVK, it calls DXGI which cause softlock
    std::atomic<bool> skipDxgiLoadChecks{false};
    std::atomic<bool> skipParentWrapping{false};
    std::atomic<bool> skipHeapCapture{false};
    std::atomic<bool> vulkanCreatingSC{false};
    std::atomic<bool> vulkanSkipHooks{false};

    // quirks
    std::vector<std::string> detectedQuirks {};

    // FFX
    std::vector<const char*> ffxUpscalerVersionNames {};
    std::vector<uint64_t> ffxUpscalerVersionIds {};
    std::vector<const char*> ffxFGVersionNames {};
    std::vector<uint64_t> ffxFGVersionIds {};
    uint32_t currentFsr4Model {};

    // Linux checks
    bool isRunningOnLinux = false;
    bool isRunningOnDXVK = false;

    // Other checks
    bool isRunningOnNvidia = false;
    bool isPascalOrOlder = false;
    bool isDxgiMode = false;
    bool isD3D12Mode = false;
    bool isWorkingAsNvngx = false;

    // Vulkan stuff
    VkInstance VulkanInstance = nullptr;

    // Vulkan FG
    IFGFeature_Vk* currentFGVk = nullptr;
    VkPhysicalDevice currentVkPhysicalDevice = nullptr;
    VkQueue currentVkQueue = nullptr;
    uint32_t currentVkQueueFamilyIndex = 0;
    VkSwapchainKHR currentVkSwapchain = nullptr;

    // -----------------------------------------------------------------------
    // THREAD-SAFE SECTION: Frame timing data
    // Access to upscaleTimes and frameTimes is protected by frameTimeMutex
    // -----------------------------------------------------------------------
    std::deque<double> upscaleTimes;
    std::deque<double> frameTimes;
    double lastFGFrameTime = 0.0;
    double presentFrameTime = 0.0;
    std::mutex frameTimeMutex;

    // -----------------------------------------------------------------------
    // THREAD-SAFE SECTION: Version check state
    // Access to version check fields is protected by versionCheckMutex
    // -----------------------------------------------------------------------
    std::mutex versionCheckMutex;
    bool versionCheckInProgress = false;
    bool versionCheckCompleted = false;
    bool updateAvailable = false;
    std::string latestVersionTag;
    std::string latestVersionUrl;
    std::string versionCheckError;

    // Swapchain info
    float screenWidth = 800.0;
    float screenHeight = 450.0;
    bool realExclusiveFullscreen = false;
    bool SCExclusiveFullscreen = false;
    bool SCAllowTearing = false;
    UINT SCLastFlags = 0;

    // HDR
    std::vector<IUnknown*> SCbuffers;
    bool isHdrActive = false;

    std::string setInputApiName;
    std::string currentInputApiName;

    bool isShuttingDown = false;
    std::set<PVOID> modulesToFree;

    // menu warnings
    bool fgSettingsChanged = false;
    bool nvngxIniDetected = false;

    bool nvngxExists = false;
    std::optional<std::wstring> nvngxReplacement = std::nullopt;
    bool libxessExists = false;
    bool fsrHooks = false;

    IFeature* currentFeature = nullptr;
    IFGFeature_Dx12* currentFG = nullptr;
    IDXGISwapChain* currentSwapchain = nullptr;
    IDXGISwapChain* currentWrappedSwapchain = nullptr;
    IDXGISwapChain* currentRealSwapchain = nullptr;
    IDXGISwapChain* currentFGSwapchain = nullptr;
    ID3D12Device* currentD3D12Device = nullptr;
    ID3D11Device* currentD3D11Device = nullptr;
    ID3D12CommandQueue* currentCommandQueue = nullptr;
    VkDevice currentVkDevice = nullptr;
    DXGI_SWAP_CHAIN_DESC currentSwapchainDesc {};

    std::vector<ID3D12Device*> d3d12Devices;
    std::vector<ID3D11Device*> d3d11Devices;
    std::unordered_map<UINT64, std::string> adapterDescs;
    std::mutex adapterDescsMutex;

    // Moved checks here to prevent circular includes
    /// <summary>
    /// Enables skipping of LoadLibrary checks
    /// </summary>
    /// <param name="dllName">Lower case dll name without `.dll` at the end. Leave blank for skipping all dll's</param>
    static void DisableChecks(UINT owner, std::string dllName = "")
    {
        UINT expected = 0;
        if (_skipOwner.compare_exchange_strong(expected, owner))
        {
            // Successfully acquired the lock
            _skipChecks = true;
            auto* ptr = new std::string(std::move(dllName));
            delete _skipDllName.exchange(ptr);
        }
        // If another thread already owns the lock, do nothing - they already set the skip
    };

    static void EnableChecks(UINT owner)
    {
        UINT expected = owner;
        if (_skipOwner.compare_exchange_strong(expected, 0))
        {
            // Successfully released the lock
            _skipChecks = false;
            auto* ptr = _skipDllName.exchange(nullptr);
            delete ptr;
        }
        // If compare_exchange failed, either another owner or already 0
    };

    static void DisableServeOriginal(UINT owner)
    {
        UINT expected = 0;
        if (_serveOwner.compare_exchange_strong(expected, owner))
        {
            _serveOriginal = false;
        }
        else if (_serveOwner == owner)
        {
            _serveOriginal = false;
            _serveOwner = 0;
        }
    };

    static void EnableServeOriginal(UINT owner)
    {
        UINT expected = 0;
        if (_serveOwner.compare_exchange_strong(expected, owner))
        {
            _serveOriginal = true;
        }
        // If already owned by someone else, do nothing
    };

    static bool SkipDllChecks() { return _skipChecks; }
    static std::string SkipDllName()
    {
        auto* ptr = _skipDllName.load();
        return ptr ? *ptr : "";
    }
    static bool ServeOriginal() { return _serveOriginal; }

  private:
    inline static std::atomic<bool> _skipChecks{false};
    inline static std::atomic<std::string*> _skipDllName{nullptr};
    inline static std::atomic<UINT> _skipOwner{0};

    inline static std::atomic<bool> _serveOriginal{false};
    inline static std::atomic<UINT> _serveOwner{0};

    State() = default;
};

class ScopedSkipSpoofing
{
  private:
    bool previousState;

  public:
    ScopedSkipSpoofing()
    {
        previousState = State::Instance().skipSpoofing.load();
        State::Instance().skipSpoofing.store(true);
    }

    ~ScopedSkipSpoofing() { State::Instance().skipSpoofing.store(previousState); }
};

class ScopedSkipDxgiLoadChecks
{
  private:
    bool previousState;

  public:
    ScopedSkipDxgiLoadChecks()
    {
        previousState = State::Instance().skipDxgiLoadChecks.load();
        State::Instance().skipDxgiLoadChecks.store(true);
    }

    ~ScopedSkipDxgiLoadChecks() { State::Instance().skipDxgiLoadChecks.store(previousState); }
};

class ScopedSkipParentWrapping
{
  private:
    bool previousState;

  public:
    ScopedSkipParentWrapping()
    {
        previousState = State::Instance().skipParentWrapping.load();
        State::Instance().skipParentWrapping.store(true);
    }

    ~ScopedSkipParentWrapping() { State::Instance().skipParentWrapping.store(previousState); }
};

class ScopedSkipHeapCapture
{
  private:
    bool previousState;

  public:
    ScopedSkipHeapCapture()
    {
        previousState = State::Instance().skipHeapCapture.load();
        State::Instance().skipHeapCapture.store(true);
    }

    ~ScopedSkipHeapCapture() { State::Instance().skipHeapCapture.store(previousState); }
};

class ScopedSkipVulkanHooks
{
  private:
    bool previousState;

  public:
    ScopedSkipVulkanHooks()
    {
        previousState = State::Instance().vulkanSkipHooks.load();
        State::Instance().vulkanSkipHooks.store(true);
    }
    ~ScopedSkipVulkanHooks() { State::Instance().vulkanSkipHooks.store(previousState); }
};

class ScopedVulkanCreatingSC
{
  private:
    bool previousState;

  public:
    ScopedVulkanCreatingSC()
    {
        previousState = State::Instance().vulkanCreatingSC.load();
        State::Instance().vulkanCreatingSC.store(true);
    }
    ~ScopedVulkanCreatingSC() { State::Instance().vulkanCreatingSC.store(previousState); }
};
