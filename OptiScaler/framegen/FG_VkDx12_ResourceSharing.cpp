#include "pch.h"

#include "FG_VkDx12_ResourceSharing.h"

#include <magic_enum.hpp>

#define SAFE_RELEASE(p)                                                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        if (p && p != nullptr)                                                                                         \
        {                                                                                                              \
            (p)->Release();                                                                                            \
            (p) = nullptr;                                                                                             \
        }                                                                                                              \
    } while ((void)0, 0)

#define SAFE_DESTROY_VK(func, device, handle, allocator)                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (handle != VK_NULL_HANDLE)                                                                                  \
        {                                                                                                              \
            func(device, handle, allocator);                                                                           \
            handle = VK_NULL_HANDLE;                                                                                   \
        }                                                                                                              \
    } while ((void)0, 0)

#define SAFE_CLOSE_HANDLE(h)                                                                                           \
    do                                                                                                                 \
    {                                                                                                                  \
        if (h != nullptr && h != INVALID_HANDLE_VALUE)                                                                 \
        {                                                                                                              \
            CloseHandle(h);                                                                                            \
            h = nullptr;                                                                                               \
        }                                                                                                              \
    } while ((void)0, 0)

// Helper function to convert Vulkan formats to DXGI formats
DXGI_FORMAT VkFormatToDxgiFormat(VkFormat vkFormat)
{
    switch (vkFormat)
    {
    // 8-bit formats
    case VK_FORMAT_R8_UNORM:
        return DXGI_FORMAT_R8_UNORM;
    case VK_FORMAT_R8_SNORM:
        return DXGI_FORMAT_R8_SNORM;
    case VK_FORMAT_R8_UINT:
        return DXGI_FORMAT_R8_UINT;
    case VK_FORMAT_R8_SINT:
        return DXGI_FORMAT_R8_SINT;
    case VK_FORMAT_R8G8_UNORM:
        return DXGI_FORMAT_R8G8_UNORM;
    case VK_FORMAT_R8G8_SNORM:
        return DXGI_FORMAT_R8G8_SNORM;
    case VK_FORMAT_R8G8_UINT:
        return DXGI_FORMAT_R8G8_UINT;
    case VK_FORMAT_R8G8_SINT:
        return DXGI_FORMAT_R8G8_SINT;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SNORM:
        return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VK_FORMAT_R8G8B8A8_UINT:
        return DXGI_FORMAT_R8G8B8A8_UINT;
    case VK_FORMAT_R8G8B8A8_SINT:
        return DXGI_FORMAT_R8G8B8A8_SINT;
    case VK_FORMAT_R8G8B8A8_SRGB:
        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    // 16-bit formats
    case VK_FORMAT_R16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    case VK_FORMAT_R16_SNORM:
        return DXGI_FORMAT_R16_SNORM;
    case VK_FORMAT_R16_UINT:
        return DXGI_FORMAT_R16_UINT;
    case VK_FORMAT_R16_SINT:
        return DXGI_FORMAT_R16_SINT;
    case VK_FORMAT_R16_SFLOAT:
        return DXGI_FORMAT_R16_FLOAT;
    case VK_FORMAT_R16G16_UNORM:
        return DXGI_FORMAT_R16G16_UNORM;
    case VK_FORMAT_R16G16_SNORM:
        return DXGI_FORMAT_R16G16_SNORM;
    case VK_FORMAT_R16G16_UINT:
        return DXGI_FORMAT_R16G16_UINT;
    case VK_FORMAT_R16G16_SINT:
        return DXGI_FORMAT_R16G16_SINT;
    case VK_FORMAT_R16G16_SFLOAT:
        return DXGI_FORMAT_R16G16_FLOAT;
    case VK_FORMAT_R16G16B16A16_UNORM:
        return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VK_FORMAT_R16G16B16A16_SNORM:
        return DXGI_FORMAT_R16G16B16A16_SNORM;
    case VK_FORMAT_R16G16B16A16_UINT:
        return DXGI_FORMAT_R16G16B16A16_UINT;
    case VK_FORMAT_R16G16B16A16_SINT:
        return DXGI_FORMAT_R16G16B16A16_SINT;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;

    // 32-bit formats
    case VK_FORMAT_R32_UINT:
        return DXGI_FORMAT_R32_UINT;
    case VK_FORMAT_R32_SINT:
        return DXGI_FORMAT_R32_SINT;
    case VK_FORMAT_R32_SFLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case VK_FORMAT_R32G32_UINT:
        return DXGI_FORMAT_R32G32_UINT;
    case VK_FORMAT_R32G32_SINT:
        return DXGI_FORMAT_R32G32_SINT;
    case VK_FORMAT_R32G32_SFLOAT:
        return DXGI_FORMAT_R32G32_FLOAT;
    case VK_FORMAT_R32G32B32_UINT:
        return DXGI_FORMAT_R32G32B32_UINT;
    case VK_FORMAT_R32G32B32_SINT:
        return DXGI_FORMAT_R32G32B32_SINT;
    case VK_FORMAT_R32G32B32_SFLOAT:
        return DXGI_FORMAT_R32G32B32_FLOAT;
    case VK_FORMAT_R32G32B32A32_UINT:
        return DXGI_FORMAT_R32G32B32A32_UINT;
    case VK_FORMAT_R32G32B32A32_SINT:
        return DXGI_FORMAT_R32G32B32A32_SINT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;

    // Depth formats
    case VK_FORMAT_D16_UNORM:
        return DXGI_FORMAT_D16_UNORM;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case VK_FORMAT_D32_SFLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

FG_VkDx12_ResourceSharing::FG_VkDx12_ResourceSharing()
{
}

FG_VkDx12_ResourceSharing::~FG_VkDx12_ResourceSharing()
{
    ReleaseResources();
}

bool FG_VkDx12_ResourceSharing::Init(VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice,
                                     ID3D12Device* d3d12Device, PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr)
{
    LOG_FUNC();

    if (vkDevice == VK_NULL_HANDLE || vkPhysicalDevice == VK_NULL_HANDLE || d3d12Device == nullptr)
    {
        LOG_ERROR("Invalid device handles provided");
        return false;
    }

    _vkDevice = vkDevice;
    _vkPhysicalDevice = vkPhysicalDevice;
    _d3d12Device = d3d12Device;

    if (!LoadVulkanFunctions(vkGetDeviceProcAddr))
    {
        LOG_ERROR("Failed to load Vulkan external memory functions");
        return false;
    }

    _initialized = true;
    return true;
}

bool FG_VkDx12_ResourceSharing::LoadVulkanFunctions(PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr)
{
    if (vkGetDeviceProcAddr == nullptr)
    {
        LOG_ERROR("vkGetDeviceProcAddr is null");
        return false;
    }

    if (_vkGetMemoryWin32HandlePropertiesKHR == nullptr)
    {
        _vkGetMemoryWin32HandlePropertiesKHR = (PFN_vkGetMemoryWin32HandlePropertiesKHR)
            vkGetDeviceProcAddr(_vkDevice, "vkGetMemoryWin32HandlePropertiesKHR");
    }

    if (_vkGetMemoryWin32HandlePropertiesKHR == nullptr)
    {
        LOG_ERROR("Failed to load vkGetMemoryWin32HandlePropertiesKHR");
        return false;
    }

    if (_vkImportSemaphoreWin32HandleKHR == nullptr)
    {
        _vkImportSemaphoreWin32HandleKHR = (PFN_vkImportSemaphoreWin32HandleKHR)
            vkGetDeviceProcAddr(_vkDevice, "vkImportSemaphoreWin32HandleKHR");
    }

    // Semaphore import function is optional - only needed for cross-API sync
    if (_vkImportSemaphoreWin32HandleKHR == nullptr)
    {
        LOG_WARN("vkImportSemaphoreWin32HandleKHR not available - cross-API sync will be limited");
    }

    return true;
}

bool FG_VkDx12_ResourceSharing::CreateResources(uint32_t width, uint32_t height,
                                                 VkFormat depthFormat, VkFormat mvFormat)
{
    LOG_FUNC();

    if (!_initialized)
    {
        LOG_ERROR("ResourceSharing not initialized");
        return false;
    }

    // Release existing resources first
    ReleaseResources();

    _width = width;
    _height = height;
    _depthFormat = depthFormat;
    _mvFormat = mvFormat;

    // Create depth shared texture
    VkImageCreateInfo depthImageInfo = {};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.format = depthFormat;
    depthImageInfo.extent.width = width;
    depthImageInfo.extent.height = height;
    depthImageInfo.extent.depth = 1;
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!CreateSharedTexture(depthImageInfo, _sharedDepthVk, _sharedDepthMemory, _sharedDepthDx12, _depthHandle, false))
    {
        LOG_ERROR("Failed to create shared depth texture");
        ReleaseResources();
        return false;
    }

    LOG_INFO("Created shared depth texture: {}x{}, format={}", width, height, magic_enum::enum_name(depthFormat));

    // Create velocity shared texture
    VkImageCreateInfo mvImageInfo = {};
    mvImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    mvImageInfo.imageType = VK_IMAGE_TYPE_2D;
    mvImageInfo.format = mvFormat;
    mvImageInfo.extent.width = width;
    mvImageInfo.extent.height = height;
    mvImageInfo.extent.depth = 1;
    mvImageInfo.mipLevels = 1;
    mvImageInfo.arrayLayers = 1;
    mvImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    mvImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    mvImageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    mvImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    mvImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (!CreateSharedTexture(mvImageInfo, _sharedVelocityVk, _sharedVelocityMemory, _sharedVelocityDx12, _velocityHandle, false))
    {
        LOG_ERROR("Failed to create shared velocity texture");
        ReleaseResources();
        return false;
    }

    LOG_INFO("Created shared velocity texture: {}x{}, format={}", width, height, magic_enum::enum_name(mvFormat));

    _resourcesCreated = true;
    return true;
}

void FG_VkDx12_ResourceSharing::ReleaseResources()
{
    ReleaseSharedTexture(_sharedDepthVk, _sharedDepthMemory, _sharedDepthDx12, _depthHandle);
    ReleaseSharedTexture(_sharedVelocityVk, _sharedVelocityMemory, _sharedVelocityDx12, _velocityHandle);

    // Release command pool and buffer
    if (_commandBuffer != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(_vkDevice, _commandPool, 1, &_commandBuffer);
        _commandBuffer = VK_NULL_HANDLE;
    }

    if (_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_vkDevice, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
    }

    // Release sync primitives
    ReleaseSyncPrimitives();

    _width = 0;
    _height = 0;
    _depthFormat = VK_FORMAT_UNDEFINED;
    _mvFormat = VK_FORMAT_UNDEFINED;
    _resourcesCreated = false;
}

void FG_VkDx12_ResourceSharing::ReleaseSharedTexture(VkImage& vulkanResource, VkDeviceMemory& vulkanMemory,
                                                      ID3D12Resource*& d3d12Resource, HANDLE& sharedHandle)
{
    SAFE_DESTROY_VK(vkDestroyImage, _vkDevice, vulkanResource, nullptr);
    SAFE_DESTROY_VK(vkFreeMemory, _vkDevice, vulkanMemory, nullptr);
    SAFE_RELEASE(d3d12Resource);
    SAFE_CLOSE_HANDLE(sharedHandle);
}

bool FG_VkDx12_ResourceSharing::CreateSharedTexture(const VkImageCreateInfo& imageInfo, VkImage& vulkanResource,
                                                    VkDeviceMemory& vulkanMemory, ID3D12Resource*& d3d12Resource,
                                                    HANDLE& sharedHandle, bool isOutput)
{
    const D3D12_HEAP_PROPERTIES d3d12HeapProperties = {
        .Type = D3D12_HEAP_TYPE_DEFAULT,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    };

    // Convert Vulkan format to DXGI format
    DXGI_FORMAT dxgiFormat = VkFormatToDxgiFormat(imageInfo.format);
    if (dxgiFormat == DXGI_FORMAT_UNKNOWN)
    {
        LOG_ERROR("Unsupported VkFormat for D3D12 interop: {} ({})", (int)imageInfo.format,
                  magic_enum::enum_name(imageInfo.format));
        return false;
    }

    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if (isOutput)
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    const D3D12_RESOURCE_DESC d3d12ResourceDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = imageInfo.extent.width,
        .Height = imageInfo.extent.height,
        .DepthOrArraySize = static_cast<uint16_t>(imageInfo.arrayLayers),
        .MipLevels = static_cast<uint16_t>(imageInfo.mipLevels),
        .Format = dxgiFormat,
        .SampleDesc = { .Count = 1, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = flags,
    };

    ID3D12Resource* createdResourceDX = nullptr;
    auto hr = _d3d12Device->CreateCommittedResource(&d3d12HeapProperties, D3D12_HEAP_FLAG_SHARED,
                                                     &d3d12ResourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                     nullptr, IID_PPV_ARGS(&createdResourceDX));

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create D3D12 committed resource: {0:x}", hr);
        return false;
    }

    HANDLE win32Handle = nullptr;
    hr = _d3d12Device->CreateSharedHandle(createdResourceDX, nullptr, GENERIC_ALL, nullptr, &win32Handle);

    if (FAILED(hr))
    {
        LOG_ERROR("Failed to create shared handle: {0:x}", hr);
        createdResourceDX->Release();
        return false;
    }

    // Vulkan external memory image creation
    const VkExternalMemoryImageCreateInfo externalMemoryImageCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
    };

    auto imageCreateInfoCopy = imageInfo;
    imageCreateInfoCopy.pNext = &externalMemoryImageCreateInfo;

    VkImage createdResourceVK = VK_NULL_HANDLE;
    if (vkCreateImage(_vkDevice, &imageCreateInfoCopy, nullptr, &createdResourceVK) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Vulkan image");
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    VkMemoryRequirements memoryRequirements = {};
    vkGetImageMemoryRequirements(_vkDevice, createdResourceVK, &memoryRequirements);

    VkMemoryWin32HandlePropertiesKHR memoryWin32HandleProperties = {};
    memoryWin32HandleProperties.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;

    if (_vkGetMemoryWin32HandlePropertiesKHR(_vkDevice, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                              win32Handle, &memoryWin32HandleProperties) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to get memory Win32 handle properties");
        vkDestroyImage(_vkDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    // Memory import setup
    const VkMemoryDedicatedAllocateInfo dedicatedAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = createdResourceVK,
        .buffer = VK_NULL_HANDLE,
    };

    const VkImportMemoryWin32HandleInfoKHR importMemoryWin32HandleInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext = &dedicatedAllocInfo,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
        .handle = win32Handle,
    };

    VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    uint32_t allowedBits = memoryRequirements.memoryTypeBits & memoryWin32HandleProperties.memoryTypeBits;

    uint32_t typeIndex = FindVulkanMemoryTypeIndex(allowedBits, memoryFlags);

    const VkMemoryAllocateInfo memoryAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importMemoryWin32HandleInfo,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = typeIndex,
    };

    if (memoryAllocInfo.memoryTypeIndex == 0xFFFFFFFF)
    {
        LOG_ERROR("Failed to find compatible memory type for shared texture. Format: {}, MemoryTypeBits: {:x}",
                  magic_enum::enum_name(imageInfo.format), memoryWin32HandleProperties.memoryTypeBits);
        vkDestroyImage(_vkDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    VkDeviceMemory createdMemory = VK_NULL_HANDLE;
    if (vkAllocateMemory(_vkDevice, &memoryAllocInfo, nullptr, &createdMemory) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to allocate Vulkan memory");
        vkDestroyImage(_vkDevice, createdResourceVK, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    if (vkBindImageMemory(_vkDevice, createdResourceVK, createdMemory, 0) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to bind image memory");
        vkDestroyImage(_vkDevice, createdResourceVK, nullptr);
        vkFreeMemory(_vkDevice, createdMemory, nullptr);
        CloseHandle(win32Handle);
        createdResourceDX->Release();
        return false;
    }

    vulkanResource = createdResourceVK;
    vulkanMemory = createdMemory;
    d3d12Resource = createdResourceDX;
    sharedHandle = win32Handle;

    // Note: The handle is stored for resource management, but can be closed after import
    // Keeping it for potential reuse or debugging

    return true;
}

uint32_t FG_VkDx12_ResourceSharing::FindVulkanMemoryTypeIndex(uint32_t memoryTypeBits, VkMemoryPropertyFlags propertyFlags)
{
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(_vkPhysicalDevice, &memoryProperties);

    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if (((1u << i) & memoryTypeBits) == 0)
            continue;

        if ((memoryProperties.memoryTypes[i].propertyFlags & propertyFlags) != propertyFlags)
            continue;

        return i;
    }

    // Fallback: try without device local
    if (propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    {
        LOG_WARN("Device-local memory not available, trying without it");
        VkMemoryPropertyFlags fallbackFlags = propertyFlags & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
        {
            if (((1u << i) & memoryTypeBits) == 0)
                continue;

            if ((memoryProperties.memoryTypes[i].propertyFlags & fallbackFlags) == fallbackFlags)
                return i;
        }
    }

    LOG_ERROR("No compatible memory type found! MemoryTypeBits={:x}, PropertyFlags={:x}", memoryTypeBits, (uint32_t)propertyFlags);
    return 0xFFFFFFFF;
}

void FG_VkDx12_ResourceSharing::ImageMemoryBarrier(VkCommandBuffer cmdBuffer, VkImage image, VkImageLayout oldLayout,
                                                    VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                                    VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                                    VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmdBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool FG_VkDx12_ResourceSharing::CopyFromVulkan(VkCommandBuffer cmdBuffer, VkImage srcDepth, VkImageLayout srcDepthLayout,
                                                VkImage srcVelocity, VkImageLayout srcVelocityLayout,
                                                uint32_t width, uint32_t height)
{
    if (!_resourcesCreated)
    {
        LOG_ERROR("Resources not created");
        return false;
    }

    if (cmdBuffer == VK_NULL_HANDLE)
    {
        LOG_ERROR("Invalid command buffer");
        return false;
    }

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    // Handle depth aspect for depth images
    VkImageSubresourceRange depthSubresourceRange = subresourceRange;
    if (_depthFormat == VK_FORMAT_D16_UNORM || _depthFormat == VK_FORMAT_D32_SFLOAT ||
        _depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || _depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
    {
        depthSubresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // Transition shared depth to transfer dst
    ImageMemoryBarrier(cmdBuffer, _sharedDepthVk, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       depthSubresourceRange);

    // Copy depth
    VkImageCopy depthCopy = {};
    depthCopy.srcSubresource.aspectMask = depthSubresourceRange.aspectMask;
    depthCopy.srcSubresource.mipLevel = 0;
    depthCopy.srcSubresource.baseArrayLayer = 0;
    depthCopy.srcSubresource.layerCount = 1;
    depthCopy.srcOffset = { 0, 0, 0 };
    depthCopy.dstSubresource.aspectMask = depthSubresourceRange.aspectMask;
    depthCopy.dstSubresource.mipLevel = 0;
    depthCopy.dstSubresource.baseArrayLayer = 0;
    depthCopy.dstSubresource.layerCount = 1;
    depthCopy.dstOffset = { 0, 0, 0 };
    depthCopy.extent.width = width;
    depthCopy.extent.height = height;
    depthCopy.extent.depth = 1;

    vkCmdCopyImage(cmdBuffer, srcDepth, srcDepthLayout, _sharedDepthVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &depthCopy);

    // Transition shared depth to shader read
    ImageMemoryBarrier(cmdBuffer, _sharedDepthVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, depthSubresourceRange);

    // Transition shared velocity to transfer dst
    ImageMemoryBarrier(cmdBuffer, _sharedVelocityVk, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       subresourceRange);

    // Copy velocity
    VkImageCopy velocityCopy = {};
    velocityCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    velocityCopy.srcSubresource.mipLevel = 0;
    velocityCopy.srcSubresource.baseArrayLayer = 0;
    velocityCopy.srcSubresource.layerCount = 1;
    velocityCopy.srcOffset = { 0, 0, 0 };
    velocityCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    velocityCopy.dstSubresource.mipLevel = 0;
    velocityCopy.dstSubresource.baseArrayLayer = 0;
    velocityCopy.dstSubresource.layerCount = 1;
    velocityCopy.dstOffset = { 0, 0, 0 };
    velocityCopy.extent.width = width;
    velocityCopy.extent.height = height;
    velocityCopy.extent.depth = 1;

    vkCmdCopyImage(cmdBuffer, srcVelocity, srcVelocityLayout, _sharedVelocityVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &velocityCopy);

    // Transition shared velocity to shader read
    ImageMemoryBarrier(cmdBuffer, _sharedVelocityVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, subresourceRange);

    return true;
}

void FG_VkDx12_ResourceSharing::SetQueue(VkQueue queue, uint32_t queueFamilyIndex)
{
    _vkQueue = queue;
    _queueFamilyIndex = queueFamilyIndex;
}

bool FG_VkDx12_ResourceSharing::CreateVulkanCommandPool()
{
    if (_vkDevice == VK_NULL_HANDLE || _queueFamilyIndex == UINT32_MAX)
    {
        LOG_ERROR("Cannot create command pool - device or queue family not set");
        return false;
    }

    if (_commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_vkDevice, _commandPool, nullptr);
        _commandPool = VK_NULL_HANDLE;
    }

    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(_vkDevice, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
    {
        LOG_ERROR("Failed to create Vulkan command pool");
        return false;
    }

    return true;
}

bool FG_VkDx12_ResourceSharing::CreateSyncPrimitives()
{
    // Create D3D12 fence for synchronization
    if (_d3d12Device && _d3d12Fence == nullptr)
    {
        auto hr = _d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&_d3d12Fence));
        if (FAILED(hr))
        {
            LOG_ERROR("Failed to create D3D12 fence: {0:x}", hr);
            return false;
        }

        _fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (_fenceEvent == nullptr)
        {
            LOG_ERROR("Failed to create fence event");
            SAFE_RELEASE(_d3d12Fence);
            return false;
        }
    }

    // Create Vulkan fence for command buffer completion
    if (_copyFence == VK_NULL_HANDLE)
    {
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start signaled

        if (vkCreateFence(_vkDevice, &fenceInfo, nullptr, &_copyFence) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan fence");
            return false;
        }
    }

    // Create Vulkan semaphore for copy completion signaling
    if (_copyCompleteSemaphore == VK_NULL_HANDLE)
    {
        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(_vkDevice, &semInfo, nullptr, &_copyCompleteSemaphore) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to create Vulkan semaphore");
            return false;
        }
    }

    _syncPrimitivesInitialized = true;
    return true;
}

void FG_VkDx12_ResourceSharing::ReleaseSyncPrimitives()
{
    // Wait for any pending Vulkan operations before destroying
    if (_copyFence != VK_NULL_HANDLE && _vkDevice != VK_NULL_HANDLE)
    {
        // Use a reasonable timeout (5 seconds) during cleanup to avoid infinite hang
        auto result = vkWaitForFences(_vkDevice, 1, &_copyFence, VK_TRUE, 5000000000ULL);
        if (result != VK_SUCCESS)
        {
            LOG_WARN("Timeout or error waiting for Vulkan fence during cleanup: {}", (UINT)result);
        }
        vkDestroyFence(_vkDevice, _copyFence, nullptr);
        _copyFence = VK_NULL_HANDLE;
    }

    if (_copyCompleteSemaphore != VK_NULL_HANDLE)
    {
        vkDestroySemaphore(_vkDevice, _copyCompleteSemaphore, nullptr);
        _copyCompleteSemaphore = VK_NULL_HANDLE;
    }

    SAFE_RELEASE(_d3d12Fence);

    if (_fenceEvent != nullptr)
    {
        CloseHandle(_fenceEvent);
        _fenceEvent = nullptr;
    }

    _fenceValue = 0;
    _lastCompletedFenceValue = 0;
    _syncPrimitivesInitialized = false;
}

VkCommandBuffer FG_VkDx12_ResourceSharing::GetCommandBuffer()
{
    if (_vkDevice == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan device not set");
        return VK_NULL_HANDLE;
    }

    if (_commandPool == VK_NULL_HANDLE)
    {
        if (!CreateVulkanCommandPool())
        {
            return VK_NULL_HANDLE;
        }
    }

    if (_commandBuffer == VK_NULL_HANDLE)
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(_vkDevice, &allocInfo, &_commandBuffer) != VK_SUCCESS)
        {
            LOG_ERROR("Failed to allocate command buffer");
            return VK_NULL_HANDLE;
        }
    }

    // Reset command buffer for reuse
    vkResetCommandBuffer(_commandBuffer, 0);

    return _commandBuffer;
}

bool FG_VkDx12_ResourceSharing::CopyDepthToShared(VkCommandBuffer cmd, VkImage srcDepth, uint32_t width, uint32_t height)
{
    if (!_resourcesCreated || _sharedDepthVk == VK_NULL_HANDLE)
    {
        LOG_ERROR("Depth shared resource not created");
        return false;
    }

    if (cmd == VK_NULL_HANDLE)
    {
        LOG_ERROR("Invalid command buffer");
        return false;
    }

    VkImageSubresourceRange depthSubresourceRange = {};
    depthSubresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    depthSubresourceRange.baseMipLevel = 0;
    depthSubresourceRange.levelCount = 1;
    depthSubresourceRange.baseArrayLayer = 0;
    depthSubresourceRange.layerCount = 1;

    // Handle depth aspect for depth images
    if (_depthFormat == VK_FORMAT_D16_UNORM || _depthFormat == VK_FORMAT_D32_SFLOAT ||
        _depthFormat == VK_FORMAT_D24_UNORM_S8_UINT || _depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
    {
        depthSubresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    // Transition source to transfer src
    ImageMemoryBarrier(cmd, srcDepth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       depthSubresourceRange);

    // Transition shared to transfer dst
    ImageMemoryBarrier(cmd, _sharedDepthVk, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       depthSubresourceRange);

    // Copy
    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = depthSubresourceRange.aspectMask;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource.aspectMask = depthSubresourceRange.aspectMask;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent.width = width;
    copyRegion.extent.height = height;
    copyRegion.extent.depth = 1;

    vkCmdCopyImage(cmd, srcDepth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _sharedDepthVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Transition shared to GENERAL for D3D12 access
    ImageMemoryBarrier(cmd, _sharedDepthVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, depthSubresourceRange);

    return true;
}

bool FG_VkDx12_ResourceSharing::CopyVelocityToShared(VkCommandBuffer cmd, VkImage srcVelocity, uint32_t width, uint32_t height)
{
    if (!_resourcesCreated || _sharedVelocityVk == VK_NULL_HANDLE)
    {
        LOG_ERROR("Velocity shared resource not created");
        return false;
    }

    if (cmd == VK_NULL_HANDLE)
    {
        LOG_ERROR("Invalid command buffer");
        return false;
    }

    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    // Transition source to transfer src
    ImageMemoryBarrier(cmd, srcVelocity, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       subresourceRange);

    // Transition shared to transfer dst
    ImageMemoryBarrier(cmd, _sharedVelocityVk, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       subresourceRange);

    // Copy
    VkImageCopy copyRegion = {};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.mipLevel = 0;
    copyRegion.srcSubresource.baseArrayLayer = 0;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.mipLevel = 0;
    copyRegion.dstSubresource.baseArrayLayer = 0;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };
    copyRegion.extent.width = width;
    copyRegion.extent.height = height;
    copyRegion.extent.depth = 1;

    vkCmdCopyImage(cmd, srcVelocity, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _sharedVelocityVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

    // Transition shared to GENERAL for D3D12 access
    ImageMemoryBarrier(cmd, _sharedVelocityVk, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, subresourceRange);

    return true;
}

bool FG_VkDx12_ResourceSharing::SynchronizeWithD3D12()
{
    if (!_syncPrimitivesInitialized)
    {
        // No sync primitives created, try to create them
        if (!CreateSyncPrimitives())
        {
            LOG_WARN("Cannot synchronize - sync primitives not available");
            return true; // Continue without sync (caller's responsibility)
        }
    }

    // Wait for Vulkan copy to complete before D3D12 access
    if (_copyFence != VK_NULL_HANDLE)
    {
        // Wait with a reasonable timeout (1 second)
        auto result = vkWaitForFences(_vkDevice, 1, &_copyFence, VK_TRUE, 1000000000ULL);
        if (result != VK_SUCCESS)
        {
            LOG_ERROR("Failed to wait for Vulkan fence: {}", (UINT)result);
            return false;
        }

        // Reset the fence for next use
        vkResetFences(_vkDevice, 1, &_copyFence);
    }

    // Signal D3D12 fence to indicate Vulkan work is complete
    if (_d3d12Fence != nullptr)
    {
        _fenceValue++;
        _d3d12Fence->Signal(_fenceValue);
        _lastCompletedFenceValue = _fenceValue;
    }

    return true;
}

bool FG_VkDx12_ResourceSharing::SubmitCopyCommand(VkCommandBuffer cmdBuffer)
{
    if (cmdBuffer == VK_NULL_HANDLE)
    {
        LOG_ERROR("Invalid command buffer for submit");
        return false;
    }

    if (_vkQueue == VK_NULL_HANDLE)
    {
        LOG_ERROR("Vulkan queue not set");
        return false;
    }

    // Ensure sync primitives are ready
    if (!_syncPrimitivesInitialized && !CreateSyncPrimitives())
    {
        LOG_ERROR("Cannot submit - sync primitives not available");
        return false;
    }

    // End command buffer if not already ended
    // (caller should have recorded commands)

    // Submit with fence for synchronization
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    // Signal semaphore when done
    if (_copyCompleteSemaphore != VK_NULL_HANDLE)
    {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &_copyCompleteSemaphore;
    }

    auto result = vkQueueSubmit(_vkQueue, 1, &submitInfo, _copyFence);
    if (result != VK_SUCCESS)
    {
        LOG_ERROR("Failed to submit Vulkan command buffer: {}", (UINT)result);
        return false;
    }

    return true;
}

bool FG_VkDx12_ResourceSharing::WaitForD3D12Access(uint64_t timeoutNs)
{
    if (_d3d12Fence == nullptr || _fenceEvent == nullptr)
        return true; // No sync needed

    // Check if D3D12 has completed work up to last completed value
    auto currentVal = _d3d12Fence->GetCompletedValue();
    if (currentVal >= _lastCompletedFenceValue)
        return true; // Already completed

    // Wait for completion
    auto hr = _d3d12Fence->SetEventOnCompletion(_lastCompletedFenceValue, _fenceEvent);
    if (FAILED(hr))
    {
        LOG_ERROR("SetEventOnCompletion failed: {0:x}", hr);
        return false;
    }

    DWORD waitMs = (timeoutNs == UINT64_MAX) ? INFINITE : static_cast<DWORD>(timeoutNs / 1000000ULL);
    auto waitResult = WaitForSingleObject(_fenceEvent, waitMs);

    if (waitResult != WAIT_OBJECT_0)
    {
        LOG_WARN("WaitForD3D12Access timeout or error: {}", waitResult);
        return false;
    }

    return true;
}
