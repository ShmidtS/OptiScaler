#pragma once

#include <framegen/IFGFeature_Vk.h>

#include <proxies/FfxApi_Proxy.h>

#include <shaders/format_transfer/FT_Vk.h>

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

    std::unique_ptr<FT_Vk> _hudlessTransfer[BUFFER_COUNT];
    VkImage _hudlessCopyResource[BUFFER_COUNT] {};
    std::unique_ptr<FT_Vk> _uiTransfer[BUFFER_COUNT];
    VkImage _uiCopyResource[BUFFER_COUNT] {};

    VkCommandBuffer _fgCommandList[BUFFER_COUNT] {};
    VkCommandPool _fgCommandPool[BUFFER_COUNT] {};

    VkSwapchainKHR _swapChain = VK_NULL_HANDLE;
    VkSurfaceKHR _swapChainSurface = VK_NULL_HANDLE;
    VkExtent2D _swapChainExtent = {};

    static FfxApiResourceState GetFfxApiState(VkImageLayout layout)
    {
        switch (layout)
        {
        case VK_IMAGE_LAYOUT_GENERAL:
            return FFX_API_RESOURCE_STATE_COMPUTE_READ;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return FFX_API_RESOURCE_STATE_RENDER_TARGET;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return FFX_API_RESOURCE_STATE_DEPTH_ATTACHMENT;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            return FFX_API_RESOURCE_STATE_PIXEL_READ;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return FFX_API_RESOURCE_STATE_COPY_SRC;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            return FFX_API_RESOURCE_STATE_COPY_DEST;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            return FFX_API_RESOURCE_STATE_PRESENT;
        default:
            return FFX_API_RESOURCE_STATE_COMMON;
        }
    }

    bool ExecuteCommandList(int index);
    bool Dispatch();
    void ConfigureFramePaceTuning();
    bool HudlessFormatTransfer(int index, VkDevice device, VkFormat targetFormat, VkResource* resource);
    bool UIFormatTransfer(int index, VkDevice device, VkCommandBuffer cmdList, VkFormat targetFormat,
                          VkResource* resource);

    void ParseVersion(const char* version_str, feature_version* _version)
    {
        const char* p = version_str;

        // Skip non-digits at front
        while (*p)
        {
            if (isdigit((unsigned char) p[0]))
            {
                if (sscanf(p, "%u.%u.%u", &_version->major, &_version->minor, &_version->patch) == 3)
                    return;
            }

            ++p;
        }

        LOG_WARN("can't parse {0}", version_str);
    }

  protected:
    void ReleaseObjects() override final;
    void CreateObjects(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice) override final;

  public:
    // IFGFeature
    const char* Name() override final;
    feature_version Version() override final;
    HWND Hwnd() override final;

    void* FrameGenerationContext() override final;
    void* SwapchainContext() override final;

    bool CreateSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                         VkSurfaceKHR surface, VkSwapchainKHR* swapChain) override final;
    bool ReleaseSwapchain(HWND hwnd) override final;

    void CreateContext(VkDevice device, VkPhysicalDevice physicalDevice, FG_Constants& fgConstants) override final;
    void Activate() override final;
    void Deactivate() override final;
    void DestroyFGContext() override final;
    bool Shutdown() override final;

    void EvaluateState(VkDevice device, FG_Constants& fgConstants) override final;

    bool Present() override final;

    bool SetResource(VkResource* inputResource) override final;
    void SetCommandQueue(FG_ResourceType type, VkQueue queue, uint32_t queueFamilyIndex) override final;

    ffxReturnCode_t DispatchCallback(ffxDispatchDescFrameGeneration* params);

    FSRFG_Vk() : IFGFeature_Vk(), IFGFeature()
    {
        //
    }

    ~FSRFG_Vk();
};
