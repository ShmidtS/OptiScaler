#pragma once
#include <pch.h>
#include "IFGFeature.h"

#include <upscalers/IFeature.h>

#include <shaders/resource_flip/RF_Vk.h>
#include <shaders/hudless_compare/HC_Vk.h>

#include <vulkan/vulkan.h>

struct VkResource
{
    FG_ResourceType type;
    VkImage image = VK_NULL_HANDLE;
    UINT top = 0;
    UINT left = 0;
    UINT64 width = 0;
    UINT height = 0;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    VkImageLayout state = VK_IMAGE_LAYOUT_GENERAL;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;

    // Vulkan-specific: image view for sampling
    VkImageView imageView = VK_NULL_HANDLE;

    // TODO: make private?
    VkImage copy = VK_NULL_HANDLE;
    VkImageView copyView = VK_NULL_HANDLE;
    int frameIndex = -1;
    bool waitingExecution = false;

    // Vulkan image creation info for FFX API
    VkImageCreateInfo createInfo = {};

    // Vulkan image copy region
    VkImageCopy copyRegion = {};

    VkImage GetResource() { return (copy == VK_NULL_HANDLE) ? image : copy; }
};

class IFGFeature_Vk : public virtual IFGFeature
{
  private:
    VkCommandBuffer _copyCommandList[BUFFER_COUNT] {};
    VkCommandPool _copyCommandPool[BUFFER_COUNT] {};
    VkFence _copyFence[BUFFER_COUNT] {};

    bool InitCopyCmdList();
    void DestroyCopyCmdList();

  protected:
    VkDevice _device = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkQueue _gameQueue = VK_NULL_HANDLE;
    uint32_t _gameQueueFamilyIndex = UINT32_MAX;

    HWND _hwnd = NULL;

    UINT64 _fgFramePresentId = 0;
    UINT64 _lastFGFramePresentId = 0;

    VkCommandBuffer _uiCommandList[BUFFER_COUNT] {};
    VkCommandPool _uiCommandPool[BUFFER_COUNT] {};
    bool _uiCommandListResetted[BUFFER_COUNT] { false, false, false, false };

    std::unordered_map<FG_ResourceType, VkResource> _frameResources[BUFFER_COUNT] {};
    std::unordered_map<FG_ResourceType, VkImage> _resourceCopy[BUFFER_COUNT] {};
    std::mutex _frMutex;

    std::unique_ptr<RF_Vk> _mvFlip;
    std::unique_ptr<RF_Vk> _depthFlip;
    std::unique_ptr<HC_Vk> _hudlessCompare;

    bool CreateImageResource(VkDevice InDevice, VkImage InSource, VkImageLayout InState,
                             VkImage* OutResource, bool UAV = false, bool depth = false);
    bool CreateImageResourceWithSize(VkDevice device, VkImage source, VkImageLayout state,
                                     VkImage* target, UINT width, UINT height, bool UAV, bool depth);
    void ImageBarrier(VkCommandBuffer InCommandList, VkImage InResource,
                      VkImageLayout InBeforeLayout, VkImageLayout InAfterLayout,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    bool CopyResource(VkCommandBuffer cmdList, VkImage source, VkImage* target,
                      VkImageLayout sourceLayout);

    void NewFrame() override final;
    void FlipResource(VkResource* resource);

  protected:
    virtual void ReleaseObjects() = 0;
    virtual void CreateObjects(VkDevice InDevice, VkPhysicalDevice InPhysicalDevice) = 0;

  public:
    virtual void* FrameGenerationContext() = 0;
    virtual void* SwapchainContext() = 0;
    virtual HWND Hwnd() = 0;

    // Vulkan doesn't use swapchain creation in the same way as DX12
    virtual bool CreateSwapchain(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                                 VkSurfaceKHR surface, VkSwapchainKHR* swapChain) = 0;

    virtual void CreateContext(VkDevice device, VkPhysicalDevice physicalDevice, FG_Constants& fgConstants) = 0;
    virtual void EvaluateState(VkDevice device, FG_Constants& fgConstants) = 0;

    virtual bool SetResource(VkResource* inputResource) = 0;
    virtual void SetCommandQueue(FG_ResourceType type, VkQueue queue, uint32_t queueFamilyIndex) = 0;

    VkCommandBuffer GetUICommandList(int index = -1);

    VkResource* GetResource(FG_ResourceType type, int index = -1);
    bool GetResourceCopy(FG_ResourceType type, VkImageLayout bufferState, VkImage output);
    VkQueue GetCommandQueue();
    uint32_t GetCommandQueueFamilyIndex();

    bool HasResource(FG_ResourceType type, int index = -1) override final;

    IFGFeature_Vk() = default;
    virtual ~IFGFeature_Vk() { DestroyCopyCmdList(); }
};
