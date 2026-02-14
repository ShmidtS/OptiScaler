#include "pch.h"

#include "Vulkan_Hooks.h"

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscaler_time/UpscalerTime_Vk.h>

#include <misc/FrameLimit.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>

#include <framegen/ffx/FSRFG_Vk.h>
#include <proxies/FfxApi_Proxy.h>
#include <inputs/FfxApi_Vk.h>

#include <vulkan/vulkan.hpp>

#include <detours/detours.h>

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkPresentMutex;

// hooking
typedef VkResult (*PFN_QueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
typedef VkResult (*PFN_CreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*,
                                           VkSwapchainKHR*);
typedef VkResult (*PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*,
                                                const VkAllocationCallbacks*, VkSurfaceKHR*);

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
// PFN_vkCmdPipelineBarrier o_vkCmdPipelineBarrier = nullptr;
PFN_QueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_CreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;

static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice);
static VkResult hkvkQueuePresentKHR(VkQueue queue, VkPresentInfoKHR* pPresentInfo);
static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
static PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device, const char* pName);

static void HookDevice(VkDevice InDevice)
{
    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks.load())
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_QueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_CreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));

    if (o_CreateSwapchainKHR)
    {
        LOG_DEBUG("Hooking VkDevice");

        // Hook
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);
        DetourAttach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

        DetourTransactionCommit();
    }
}

// Moved to VulkanwDx12_Hooks.cpp !!
// static void hkvkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
//                                   VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
//                                   uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
//                                   uint32_t bufferMemoryBarrierCount,
//                                   const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t
//                                   imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers)
//{
//    if (State::Instance().gameQuirks & GameQuirk::VulkanDLSSBarrierFixup &&
//        (!State::Instance().isRunningOnNvidia || State::Instance().isPascalOrOlder))
//    {
//        // AMD drivers on the cards around RDNA2 didn't treat VK_IMAGE_LAYOUT_UNDEFINED in the same way Nvidia does.
//        // Doesn't seem like a bug, just a different way of handling an UB but we need to adjust.
//
//        // DLSSG Present
//        if (imageMemoryBarrierCount == 2)
//        {
//            if (pImageMemoryBarriers[0].oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
//                pImageMemoryBarriers[0].newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
//                pImageMemoryBarriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
//                pImageMemoryBarriers[1].newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
//            {
//                LOG_TRACE("Changing an UNDEFINED barrier in DLSSG Present");
//
//                VkImageMemoryBarrier newImageBarriers[2];
//                std::memcpy(newImageBarriers, pImageMemoryBarriers, sizeof(newImageBarriers));
//
//                newImageBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
//
//                return o_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
//                                              memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
//                                              pBufferMemoryBarriers, imageMemoryBarrierCount, newImageBarriers);
//            }
//        }
//
//        // DLSS
//        // Those are already in the correct layouts
//        if (imageMemoryBarrierCount == 4)
//        {
//            // In the Voyagers update, the 2nd oldLayout has changed
//            if (pImageMemoryBarriers[0].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
//                // pImageMemoryBarriers[1].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
//                pImageMemoryBarriers[2].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
//                pImageMemoryBarriers[3].oldLayout == VK_IMAGE_LAYOUT_UNDEFINED)
//            {
//                LOG_TRACE("Removing an UNDEFINED barrier in DLSS");
//                return;
//            }
//        }
//    }
//
//    return o_vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount,
//                                  pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers,
//                                  imageMemoryBarrierCount, pImageMemoryBarriers);
//}

static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    LOG_FUNC();

    if (pCreateInfo == nullptr)
    {
        LOG_ERROR("pCreateInfo is nullptr");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    auto result = o_vkCreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    // && procHwnd == pCreateInfo->hwnd) // On linux sometimes procHwnd != pCreateInfo->hwnd
    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks.load())
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _instance = instance;
        State::Instance().VulkanInstance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);
        _hwnd = pCreateInfo->hwnd;
        LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkCreateInstance(VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    VulkanSpoofing::hkvkCreateInstance(pCreateInfo, pAllocator, pInstance);

    VkResult result;
    {
        ScopedSkipSpoofing skipSpoofing {};
        result = o_vkCreateInstance(pCreateInfo, pAllocator, pInstance);
    }

    if (result == VK_SUCCESS)
    {
        State::Instance().VulkanInstance = *pInstance;
        LOG_DEBUG("State::Instance().VulkanInstance captured: {0:X}", (UINT64) State::Instance().VulkanInstance);

#ifdef VULKAN_DEBUG_LAYER
        auto address = vkGetInstanceProcAddr(State::Instance().VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) address;
        VkDebugUtilsMessengerEXT debugMessenger;
        vkCreateDebugUtilsMessengerEXT(State::Instance().VulkanInstance, &VulkanSpoofing::debugCreateInfo, nullptr,
                                       &debugMessenger);
#endif
    }

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks.load())
    {
        MenuOverlayVk::DestroyVulkanObjects(false);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    VulkanSpoofing::hkvkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    auto result = o_vkCreateDevice(physicalDevice, pCreateInfo, pAllocator, pDevice);

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks && Config::Instance()->OverlayMenu.value())
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _PD = physicalDevice;
        LOG_DEBUG("_PD captured: {0:X}", (UINT64) _PD);
        _device = *pDevice;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);
        HookDevice(_device);

        ScopedSkipSpoofing skipSpoofing {};

        VkPhysicalDeviceProperties prop {};
        vkGetPhysicalDeviceProperties(physicalDevice, &prop);

        auto szName = std::string(prop.deviceName);

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*pDevice] = szName;
    }

#ifdef USE_QUEUE_SUBMIT_2_KHR
    if (result == VK_SUCCESS)
        hkvkGetDeviceProcAddr(*pDevice, "vkQueueSubmit2KHR");
#endif

    LOG_FUNC_RESULT(result);

    return result;
}

static VkResult hkvkQueuePresentKHR(VkQueue queue, VkPresentInfoKHR* pPresentInfo)
{
    LOG_FUNC();

    // get upscaler time
    UpscalerTimeVk::ReadUpscalingTime(_device);

    if (!State::Instance().isRunningOnDXVK)
        State::Instance().swapchainApi = Vulkan;

    // Tick feature to let it know if it's frozen
    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)
        currentFeature->TickFrozenCheck();

    // Vulkan Frame Generation Present
    if (State::Instance().currentFGVk != nullptr)
    {
        // Check if we have FG resources from FfxApi
        if (FfxApiVk_HasFGResources())
        {
            VkImage depthImage = VK_NULL_HANDLE;
            VkImage mvImage = VK_NULL_HANDLE;
            uint32_t width = 0, height = 0;

            FfxApiVk_GetFGResources(&depthImage, &mvImage, &width, &height);

            // Set depth resource
            VkResource depthRes {};
            depthRes.type = FG_ResourceType::Depth;
            depthRes.image = depthImage;
            depthRes.width = width;
            depthRes.height = height;
            depthRes.validity = FG_ResourceValidity::ValidNow;
            State::Instance().currentFGVk->SetResource(&depthRes);

            // Set motion vector resource
            VkResource mvRes {};
            mvRes.type = FG_ResourceType::Velocity;
            mvRes.image = mvImage;
            mvRes.width = width;
            mvRes.height = height;
            mvRes.validity = FG_ResourceValidity::ValidNow;
            State::Instance().currentFGVk->SetResource(&mvRes);
        }

        State::Instance().currentFGVk->Present();
    }

    // render menu if needed
    if (!MenuOverlayVk::QueuePresent(queue, pPresentInfo))
    {
        LOG_ERROR("QueuePresent: false!");
        return VK_ERROR_OUT_OF_DATE_KHR;
    }

    ReflexHooks::update(false, true);

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    auto result = o_QueuePresentKHR(queue, pPresentInfo);

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps)
        FrameLimit::sleep(false);

    LOG_FUNC_RESULT(result);
    return result;
}

static VkResult hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    LOG_FUNC();

    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofing skipSpoofing {};
        result = o_CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    }

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks.load())
    {
        State::Instance().screenWidth = static_cast<float>(pCreateInfo->imageExtent.width);
        State::Instance().screenHeight = static_cast<float>(pCreateInfo->imageExtent.height);

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        MenuOverlayVk::CreateSwapchain(device, _PD, _instance, _hwnd, pCreateInfo, pAllocator, pSwapchain);

        // Initialize Vulkan Frame Generation if enabled
        // Note: FFX Vulkan FG requires proper FfxApi initialization via FfxApiProxy::InitFfxVk()
        if (State::Instance().currentFGVk == nullptr && Config::Instance()->FGEnabled.value_or_default() &&
            (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::DLSSG))
        {
            // Check that physical device is available
            if (_PD == VK_NULL_HANDLE)
            {
                LOG_ERROR("Cannot create FSRFG_Vk: physical device not captured");
            }
            else if (!FfxApiProxy::IsVkFGReady())
            {
                // FFX VK doesn't support FG (< 3.2), recommend Upscaler FG with DX12 interop
                auto version = FfxApiProxy::VersionVk();
                LOG_ERROR("===========================================");
                LOG_ERROR("Vulkan Frame Generation not available");
                LOG_ERROR("FFX VK version: {}.{}.{} (requires 3.2+)", version.major, version.minor, version.patch);
                LOG_ERROR("");
                LOG_ERROR("For Vulkan games, use these settings:");
                LOG_ERROR("  FG Source: Upscaler FG (OptiFG)");
                LOG_ERROR("  FG Output: FSR FG or DLSSG");
                LOG_ERROR("");
                LOG_ERROR("This will use DX12 interop for Frame Generation");
                LOG_ERROR("Required: amd_fidelityfx_dx12.dll or amd_fidelityfx_framegeneration_dx12.dll");
                LOG_ERROR("===========================================");
            }
            else
            {
                LOG_DEBUG("Creating FSRFG_Vk instance for Vulkan Frame Generation");

                // Get queue family index for graphics
                uint32_t queueFamilyIndex = 0;
                uint32_t queueFamilyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(_PD, &queueFamilyCount, nullptr);

                if (queueFamilyCount > 0)
                {
                    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
                    vkGetPhysicalDeviceQueueFamilyProperties(_PD, &queueFamilyCount, queueFamilies.data());

                    for (uint32_t i = 0; i < queueFamilyCount; i++)
                    {
                        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        {
                            queueFamilyIndex = i;
                            break;
                        }
                    }
                }

                // Get graphics queue
                VkQueue graphicsQueue = VK_NULL_HANDLE;
                vkGetDeviceQueue(device, queueFamilyIndex, 0, &graphicsQueue);

                if (graphicsQueue == VK_NULL_HANDLE)
                {
                    LOG_ERROR("Cannot create FSRFG_Vk: graphics queue not available");
                }
                else
                {
                    // Create FSRFG_Vk instance
                    auto fg = new FSRFG_Vk();
                    fg->SetQueue(FG_ResourceType::Depth, graphicsQueue, queueFamilyIndex);

                    // Create FG swapchain
                    if (fg->CreateSwapchain(device, _PD, pCreateInfo, pSwapchain))
                    {
                        // Create FG context
                        FG_Constants fgConstants {};
                        fgConstants.displayWidth = pCreateInfo->imageExtent.width;
                        fgConstants.displayHeight = pCreateInfo->imageExtent.height;

                        if (fg->CreateContext(device, _PD, _instance, fgConstants))
                        {
                            fg->Activate();
                            State::Instance().currentFGVk = fg;
                            LOG_INFO("FSRFG_Vk initialized successfully");
                        }
                        else
                        {
                            LOG_ERROR("Failed to create FSRFG_Vk context");
                            delete fg;
                        }
                    }
                    else
                    {
                        LOG_ERROR("Failed to create FSRFG_Vk swapchain");
                        delete fg;
                    }

                    // Store device info for FG
                    State::Instance().currentVkDevice = device;
                    State::Instance().currentVkPhysicalDevice = _PD;
                    State::Instance().currentVkSwapchain = *pSwapchain;
                }
            }
        }
    }

    LOG_FUNC_RESULT(result);
    return result;
}

PFN_vkVoidFunction hkvkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    if (pName == nullptr)
        return VK_NULL_HANDLE;

    auto orgFunc = o_vkGetInstanceProcAddr(instance, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetInstanceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    if (pName == nullptr)
        return VK_NULL_HANDLE;

    auto orgFunc = o_vkGetDeviceProcAddr(device, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result != VK_NULL_HANDLE)
        return result;

    return orgFunc;
}

void VulkanHooks::Hook(HMODULE vulkan1)
{
    VulkanSpoofing::HookForVulkanSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanExtensionSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanVRAMSpoofing(vulkan1);

    if (o_vkCreateDevice != nullptr)
        return;

    FARPROC address = nullptr;

    o_vkCreateDevice = (PFN_vkCreateDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateDevice");
    o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateInstance");

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetInstanceProcAddr");
    o_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetDeviceProcAddr");
    o_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");
    o_vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR) address;

    // address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCmdPipelineBarrier");
    // o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) address;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_vkCreateDevice != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkGetInstanceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetInstanceProcAddr, hkvkGetInstanceProcAddr);

    if (o_vkGetDeviceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetDeviceProcAddr, hkvkGetDeviceProcAddr);

    if (o_vkCreateInstance != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    DetourTransactionCommit();
}

void VulkanHooks::Unhook()
{
    // Cleanup Vulkan Frame Generation
    if (State::Instance().currentFGVk != nullptr)
    {
        delete State::Instance().currentFGVk;
        State::Instance().currentFGVk = nullptr;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_QueuePresentKHR != nullptr)
        DetourDetach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);

    if (o_CreateSwapchainKHR != nullptr)
        DetourDetach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);

    if (o_vkCreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkCreateInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourDetach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    DetourTransactionCommit();
}
