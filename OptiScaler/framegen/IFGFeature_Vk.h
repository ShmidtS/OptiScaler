#pragma once
#include "SysUtils.h"
#include "IFGFeature.h"

#include <upscalers/IFeature.h>

#include <vulkan/vulkan.h>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

struct VkResource
{
    FG_ResourceType type;
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags accessFlags = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
    VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
    FG_ResourceValidity validity = FG_ResourceValidity::ValidNow;

    // Copy resources for FG
    VkImage copyImage = VK_NULL_HANDLE;
    VkImageView copyImageView = VK_NULL_HANDLE;
    VkDeviceMemory copyMemory = VK_NULL_HANDLE;
    int frameIndex = -1;
    bool waitingExecution = false;

    VkImage GetImage() { return (copyImage != VK_NULL_HANDLE) ? copyImage : image; }
    VkImageView GetImageView() { return (copyImageView != VK_NULL_HANDLE) ? copyImageView : imageView; }
};

class IFGFeature_Vk : public virtual IFGFeature
{
  private:
    VkCommandBuffer _copyCommandBuffer[BUFFER_COUNT] { VK_NULL_HANDLE };
    VkCommandPool _copyCommandPool[BUFFER_COUNT] { VK_NULL_HANDLE };

    bool InitCopyCmdBuffer();
    void DestroyCopyCmdBuffer();
    void DestroyFrameResources();

  protected:
    VkDevice _device = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkInstance _instance = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkQueue _gameQueue = VK_NULL_HANDLE;
    uint32_t _queueFamilyIndex = 0;

    HWND _hwnd = NULL;

    uint64_t _fgFramePresentId = 0;
    uint64_t _lastFGFramePresentId = 0;

    VkCommandBuffer _uiCommandBuffer[BUFFER_COUNT] { VK_NULL_HANDLE };
    VkCommandPool _uiCommandPool[BUFFER_COUNT] { VK_NULL_HANDLE };
    bool _uiCommandBufferResetted[BUFFER_COUNT] { false, false, false, false };

    std::unordered_map<FG_ResourceType, VkResource> _frameResources[BUFFER_COUNT] {};
    std::unordered_map<FG_ResourceType, VkImage> _resourceCopy[BUFFER_COUNT] {};
    std::mutex _frMutex;

    PFN_vkGetInstanceProcAddr _vkGIPA = nullptr;
    PFN_vkGetDeviceProcAddr _vkGDPA = nullptr;

    bool CreateBufferResource(VkDevice device, VkImage source, VkImageLayout layout, VkFormat format,
                              uint32_t width, uint32_t height, VkImage* outImage, VkImageView* outImageView,
                              VkDeviceMemory* outMemory, bool UAV = false, bool depth = false);
    void ImageMemoryBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                            VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                            VkImageSubresourceRange subresourceRange);
    bool CopyVkImage(VkCommandBuffer cmdBuffer, VkImage source, VkImageLayout sourceLayout,
                     VkImage* target, VkDeviceMemory* targetMemory, uint32_t width, uint32_t height, VkFormat format);

    void NewFrame() override final;
    void FlipResource(VkResource* resource);

  protected:
    virtual void ReleaseObjects() = 0;
    virtual void CreateObjects(VkDevice device) = 0;

  public:
    virtual void* FrameGenerationContext() = 0;
    virtual void* SwapchainContext() = 0;
    virtual HWND Hwnd() = 0;

    virtual bool CreateSwapchain(VkDevice device, VkPhysicalDevice physicalDevice,
                                 const VkSwapchainCreateInfoKHR* createInfo,
                                 VkSwapchainKHR* swapchain) = 0;

    virtual bool CreateContext(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkInstance instance, FG_Constants& fgConstants) = 0;
    virtual void EvaluateState(VkDevice device, FG_Constants& fgConstants) = 0;

    virtual bool SetResource(VkResource* inputResource) = 0;
    virtual void SetQueue(FG_ResourceType type, VkQueue queue, uint32_t familyIndex) = 0;

    VkCommandBuffer GetUICommandBuffer(int index = -1);

    VkResource* GetResource(FG_ResourceType type, int index = -1);
    bool GetResourceCopy(FG_ResourceType type, VkImageLayout layout, VkImage output);
    VkQueue GetQueue();
    uint32_t GetQueueFamilyIndex();

    bool HasResource(FG_ResourceType type, int index = -1) override final;

    IFGFeature_Vk() = default;
    virtual ~IFGFeature_Vk()
    {
        DestroyFrameResources();
        DestroyCopyCmdBuffer();
    }
};
