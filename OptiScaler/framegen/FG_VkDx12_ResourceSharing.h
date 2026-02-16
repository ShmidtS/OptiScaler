#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <vulkan/vulkan.h>

#ifdef VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan_win32.h>
#endif

#include <stdint.h>

/**
 * Vulkan-D3D12 Shared Texture Manager for Frame Generation Resources.
 * Manages shared textures for depth and velocity between Vulkan and D3D12
 * using VK_KHR_external_memory for resource sharing.
 */
class FG_VkDx12_ResourceSharing
{
public:
    FG_VkDx12_ResourceSharing();
    ~FG_VkDx12_ResourceSharing();

    // Disable copy
    FG_VkDx12_ResourceSharing(const FG_VkDx12_ResourceSharing&) = delete;
    FG_VkDx12_ResourceSharing& operator=(const FG_VkDx12_ResourceSharing&) = delete;

    /**
     * Initialize the resource sharing manager.
     * @param vkDevice Vulkan device handle
     * @param vkPhysicalDevice Vulkan physical device handle
     * @param d3d12Device D3D12 device pointer
     * @param vkGetDeviceProcAddr Function pointer for loading Vulkan device functions
     * @return true if initialization succeeded
     */
    bool Init(VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice, ID3D12Device* d3d12Device,
              PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr);

    /**
     * Set the Vulkan queue for command buffer submission.
     * @param queue Vulkan queue handle
     * @param queueFamilyIndex Queue family index for command pool creation
     */
    void SetQueue(VkQueue queue, uint32_t queueFamilyIndex);

    /**
     * Create shared resources for depth and velocity.
     * @param width Texture width
     * @param height Texture height
     * @param depthFormat Vulkan format for depth texture
     * @param mvFormat Vulkan format for motion vectors texture
     * @return true if resource creation succeeded
     */
    bool CreateResources(uint32_t width, uint32_t height, VkFormat depthFormat, VkFormat mvFormat);

    /**
     * Release all shared resources.
     */
    void ReleaseResources();

    /**
     * Get the D3D12 depth resource.
     * @return Pointer to ID3D12Resource for depth, or nullptr if not created
     */
    ID3D12Resource* GetDepthDx12Resource() const { return _sharedDepthDx12; }

    /**
     * Get the D3D12 velocity resource.
     * @return Pointer to ID3D12Resource for velocity, or nullptr if not created
     */
    ID3D12Resource* GetVelocityDx12Resource() const { return _sharedVelocityDx12; }

    /**
     * Get the Vulkan depth image.
     * @return VkImage handle for depth, or VK_NULL_HANDLE if not created
     */
    VkImage GetDepthVkImage() const { return _sharedDepthVk; }

    /**
     * Get the Vulkan velocity image.
     * @return VkImage handle for velocity, or VK_NULL_HANDLE if not created
     */
    VkImage GetVelocityVkImage() const { return _sharedVelocityVk; }

    /**
     * Copy data from Vulkan source images to shared resources.
     * @param cmdBuffer Vulkan command buffer to record copy commands
     * @param srcDepth Source depth image
     * @param srcDepthLayout Layout of source depth image
     * @param srcVelocity Source velocity image
     * @param srcVelocityLayout Layout of source velocity image
     * @param width Width of the region to copy
     * @param height Height of the region to copy
     * @return true if copy commands were recorded successfully
     */
    bool CopyFromVulkan(VkCommandBuffer cmdBuffer, VkImage srcDepth, VkImageLayout srcDepthLayout,
                        VkImage srcVelocity, VkImageLayout srcVelocityLayout,
                        uint32_t width, uint32_t height);

    /**
     * Copy depth data from Vulkan source to shared resource.
     * Transitions source to TRANSFER_SRC_OPTIMAL, shared to TRANSFER_DST_OPTIMAL,
     * performs copy, then transitions shared to GENERAL for D3D12 access.
     * @param cmd Vulkan command buffer to record copy commands
     * @param srcDepth Source depth image
     * @param width Width of the region to copy
     * @param height Height of the region to copy
     * @return true if copy commands were recorded successfully
     */
    bool CopyDepthToShared(VkCommandBuffer cmd, VkImage srcDepth, uint32_t width, uint32_t height);

    /**
     * Copy velocity data from Vulkan source to shared resource.
     * Transitions source to TRANSFER_SRC_OPTIMAL, shared to TRANSFER_DST_OPTIMAL,
     * performs copy, then transitions shared to GENERAL for D3D12 access.
     * @param cmd Vulkan command buffer to record copy commands
     * @param srcVelocity Source velocity image
     * @param width Width of the region to copy
     * @param height Height of the region to copy
     * @return true if copy commands were recorded successfully
     */
    bool CopyVelocityToShared(VkCommandBuffer cmd, VkImage srcVelocity, uint32_t width, uint32_t height);

    /**
     * Synchronize with D3D12 - wait for copy completion.
     * Uses Vulkan semaphore and D3D12 fence for synchronization.
     * @return true if synchronization succeeded
     */
    bool SynchronizeWithD3D12();

    /**
     * Get a command buffer for copy operations.
     * Creates command pool if needed and allocates command buffer.
     * @return Command buffer handle, or VK_NULL_HANDLE on failure
     */
    VkCommandBuffer GetCommandBuffer();

    /**
     * Get the Vulkan semaphore for copy completion signaling.
     * @return VkSemaphore handle for copy completion
     */
    VkSemaphore GetCopyCompleteSemaphore() const { return _copyCompleteSemaphore; }

    /**
     * Get the D3D12 fence for synchronization.
     * @return ID3D12Fence pointer for D3D12 synchronization
     */
    ID3D12Fence* GetD3D12Fence() const { return _d3d12Fence; }

    /**
     * Check if resources are initialized.
     * @return true if resources are ready for use
     */
    bool IsInitialized() const { return _initialized; }

    /**
     * Get current resource dimensions.
     * @param outWidth Output width
     * @param outHeight Output height
     */
    void GetDimensions(uint32_t& outWidth, uint32_t& outHeight) const
    {
        outWidth = _width;
        outHeight = _height;
    }

private:
    // Vulkan device handles
    VkDevice _vkDevice = VK_NULL_HANDLE;
    VkPhysicalDevice _vkPhysicalDevice = VK_NULL_HANDLE;

    // D3D12 device
    ID3D12Device* _d3d12Device = nullptr;

    // Vulkan external memory function pointers
    PFN_vkGetMemoryWin32HandlePropertiesKHR _vkGetMemoryWin32HandlePropertiesKHR = nullptr;

    // Shared depth resources
    VkImage _sharedDepthVk = VK_NULL_HANDLE;
    VkDeviceMemory _sharedDepthMemory = VK_NULL_HANDLE;
    ID3D12Resource* _sharedDepthDx12 = nullptr;
    HANDLE _depthHandle = nullptr;

    // Shared velocity resources
    VkImage _sharedVelocityVk = VK_NULL_HANDLE;
    VkDeviceMemory _sharedVelocityMemory = VK_NULL_HANDLE;
    ID3D12Resource* _sharedVelocityDx12 = nullptr;
    HANDLE _velocityHandle = nullptr;

    // Resource dimensions and formats
    uint32_t _width = 0;
    uint32_t _height = 0;
    VkFormat _depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat _mvFormat = VK_FORMAT_UNDEFINED;

    // State
    bool _initialized = false;
    bool _resourcesCreated = false;

    // Command pool and buffer for copy operations
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkCommandBuffer _commandBuffer = VK_NULL_HANDLE;
    VkQueue _vkQueue = VK_NULL_HANDLE;
    uint32_t _queueFamilyIndex = 0;

    // Synchronization primitives
    VkSemaphore _copyCompleteSemaphore = VK_NULL_HANDLE;
    ID3D12Fence* _d3d12Fence = nullptr;
    HANDLE _fenceEvent = nullptr;
    uint64_t _fenceValue = 0;
    bool _useTimelineSemaphore = false;

    // Vulkan function pointers for semaphore interop
    PFN_vkImportSemaphoreWin32HandleKHR _vkImportSemaphoreWin32HandleKHR = nullptr;

    // Internal helper methods
    bool LoadVulkanFunctions(PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr);
    bool CreateVulkanCommandPool();
    bool CreateSyncPrimitives();
    bool CreateSharedTexture(const VkImageCreateInfo& imageInfo, VkImage& vulkanResource,
                            VkDeviceMemory& vulkanMemory, ID3D12Resource*& d3d12Resource,
                            HANDLE& sharedHandle, bool isOutput);
    uint32_t FindVulkanMemoryTypeIndex(uint32_t memoryTypeBits, VkMemoryPropertyFlags propertyFlags);
    void ImageMemoryBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                           VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                           VkImageSubresourceRange subresourceRange);
    void ReleaseSharedTexture(VkImage& vulkanResource, VkDeviceMemory& vulkanMemory,
                             ID3D12Resource*& d3d12Resource, HANDLE& sharedHandle);
    void ReleaseSyncPrimitives();
};

// Helper function to convert Vulkan formats to DXGI formats
DXGI_FORMAT VkFormatToDxgiFormat(VkFormat vkFormat);
