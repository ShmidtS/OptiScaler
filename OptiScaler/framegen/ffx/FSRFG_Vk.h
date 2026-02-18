#pragma once
#include "SysUtils.h"
#include <framegen/IFGFeature_Vk.h>
#include <proxies/FfxApi_Proxy.h>

#include <ffx_framegeneration.h>
#include <vk/ffx_api_vk.h>

class FSRFG_Vk : public virtual IFGFeature_Vk
{
  private:
    ffxContext _swapChainContext = nullptr;
    ffxContext _fgContext = nullptr;
    FfxApiSurfaceFormat _lastHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
    FfxApiSurfaceFormat _usingHudlessFormat = FFX_API_SURFACE_FORMAT_UNKNOWN;
    feature_version _version { 0, 0, 0 };

    uint32_t _maxRenderWidth = 0;
    uint32_t _maxRenderHeight = 0;

    VkCommandBuffer _fgCommandBuffer[BUFFER_COUNT] { VK_NULL_HANDLE };
    VkCommandPool _fgCommandPool[BUFFER_COUNT] { VK_NULL_HANDLE };

    // FG swapchain images
    std::vector<VkImage> _fgSwapchainImages;
    std::vector<VkImageView> _fgSwapchainImageViews;

    bool ExecuteCommandBuffer(int index);
    bool Dispatch();
    void ConfigureFramePaceTuning();

    void ParseVersion(const char* version_str, feature_version* _version)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                // Use %5u to limit field width and prevent integer overflow
                if (sscanf(p, "%5u.%5u.%5u", &_version->major, &_version->minor, &_version->patch) == 3)
                    return;
            }

            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

  protected:
    void ReleaseObjects() override final;
    void CreateObjects(VkDevice device) override final;

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;
    HWND Hwnd() override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    bool CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice,
                         const VkSwapchainCreateInfoKHR* createInfo,
                         VkSwapchainKHR* swapchain) override final;
    bool ReleaseSwapchain(HWND hwnd) override final;

    bool CreateContext(VkDevice device, VkPhysicalDevice physicalDevice,
                       VkInstance instance, FG_Constants& fgConstants) override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool Shutdown() override final;

    void EvaluateState(VkDevice device, FG_Constants& fgConstants) override final;

    bool Present() override final;

    bool SetResource(VkResource* inputResource) override final;
    void SetQueue(FG_ResourceType type, VkQueue queue, uint32_t familyIndex) override final;
    bool SetInterpolatedFrameCount(UINT interpolatedFrameCount) override;

    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);

    FSRFG_Vk(UINT framesToInterpolate = 1) : IFGFeature_Vk(), IFGFeature(framesToInterpolate)
    {
        //
    }

    ~FSRFG_Vk();
};
