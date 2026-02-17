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
#include <framegen/ffx/FSRFG_Dx12.h>
#include <framegen/FG_VkDx12_ResourceSharing.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <proxies/DXGI_Proxy.h>
#include <inputs/FfxApi_Vk.h>
#include <hooks/FG_Hooks.h>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <vulkan/vulkan.hpp>

#include <detours/detours.h>

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;

static std::mutex _vkDeviceMutex;

// DX12 interop FG state
static ID3D12Device* _vkDx12Device = nullptr;
static ID3D12CommandQueue* _vkDx12CommandQueue = nullptr;
static IDXGISwapChain* _vkDx12Swapchain = nullptr;
static uint32_t _vkDx12SwapchainWidth = 0;
static uint32_t _vkDx12SwapchainHeight = 0;
static DXGI_FORMAT _vkDx12SwapchainFormat = DXGI_FORMAT_UNKNOWN;
static HWND _vkDx12HiddenHwnd = nullptr;  // Hidden window for DX12 swapchain
static const wchar_t* _vkDx12HiddenWindowClass = L"OptiScaler_DX12FG_Hidden";

// Vulkan-DX12 resource sharing for FG
static FG_VkDx12_ResourceSharing _vkDx12ResourceSharing;

// Create hidden window for DX12 FG swapchain
static HWND CreateHiddenWindowForFG()
{
    // Register window class if not already registered
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = DefWindowProcW;
    wcex.hInstance = GetModuleHandle(nullptr);
    wcex.lpszClassName = _vkDx12HiddenWindowClass;

    RegisterClassExW(&wcex);

    // Create hidden window (not visible, used only for swapchain)
    HWND hwnd = CreateWindowExW(
        0,
        _vkDx12HiddenWindowClass,
        L"OptiScaler_DX12FG_Hidden",
        WS_OVERLAPPED,
        0, 0, 1, 1,  // Minimal size
        nullptr, nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (hwnd)
        LOG_INFO("Created hidden window for DX12 FG swapchain: {:X}", (size_t)hwnd);
    else
        LOG_ERROR("Failed to create hidden window for DX12 FG: {:X}", GetLastError());

    return hwnd;
}

// Helper function to get hardware adapter for D3D12 device creation
static void GetHardwareAdapter(IDXGIFactory1* pFactory, IDXGIAdapter** ppAdapter, D3D_FEATURE_LEVEL featureLevel)
{
    *ppAdapter = nullptr;

    IDXGIAdapter1* adapter = nullptr;
    IDXGIFactory6* factory6 = nullptr;

    if (SUCCEEDED(pFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        for (UINT adapterIndex = 0;
             DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                adapter = nullptr;
                continue;
            }

            *ppAdapter = adapter;
            break;
        }
        factory6->Release();
    }
    else
    {
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter->Release();
                adapter = nullptr;
                continue;
            }

            *ppAdapter = adapter;
            break;
        }
    }
}

// Create D3D12 device and command queue for Vulkan-DX12 interop FG
static bool CreateVkDx12DeviceAndSwapchain(HWND hwnd, uint32_t width, uint32_t height, VkFormat vkFormat)
{
    LOG_FUNC();

    ScopedSkipSpoofing skipSpoofing {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    // Initialize proxies
    D3d12Proxy::Init();
    DxgiProxy::Init();

    // Convert Vulkan format to DXGI format
    auto dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // Default
    switch (vkFormat)
    {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            dxgiFormat = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        default:
            LOG_WARN("Unhandled Vulkan format {}, using R8G8B8A8_UNORM", (int)vkFormat);
            break;
    }

    // Check if we need to recreate swapchain
    if (_vkDx12Swapchain != nullptr && _vkDx12SwapchainWidth == width && _vkDx12SwapchainHeight == height && _vkDx12SwapchainFormat == dxgiFormat)
    {
        LOG_DEBUG("DX12 interop swapchain already exists with correct dimensions");
        return true;
    }

    // Create D3D12 device if not exists
    if (_vkDx12Device == nullptr)
    {
        IDXGIFactory2* factory = nullptr;
        HRESULT result;

        if (DxgiProxy::Module() == nullptr)
            result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        else
            result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

        if (result != S_OK)
        {
            LOG_ERROR("Can't create DXGI factory: {:X}", (UINT)result);
            return false;
        }

        IDXGIAdapter* adapter = nullptr;
        GetHardwareAdapter(factory, &adapter, D3D_FEATURE_LEVEL_12_0);

        if (adapter == nullptr)
            LOG_WARN("Can't get hardware adapter, will try nullptr");

        if (D3d12Proxy::Module() == nullptr)
            result = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_vkDx12Device));
        else
            result = D3d12Proxy::D3D12CreateDevice_()(adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_vkDx12Device));

        if (result != S_OK || _vkDx12Device == nullptr)
        {
            LOG_ERROR("Can't create D3D12 device: {:X}", (UINT)result);
            if (factory) factory->Release();
            return false;
        }

        if (adapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc {};
            if (adapter->GetDesc(&desc) == S_OK)
            {
                char adapterName[128];
                wcstombs(adapterName, desc.Description, sizeof(adapterName));
                LOG_INFO("D3D12 interop device created with adapter: {}", adapterName);
            }
            adapter->Release();
        }

        if (factory != nullptr)
            factory->Release();
    }

    // Create command queue if not exists
    if (_vkDx12CommandQueue == nullptr)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        HRESULT result = _vkDx12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_vkDx12CommandQueue));
        if (result != S_OK || _vkDx12CommandQueue == nullptr)
        {
            LOG_ERROR("Can't create D3D12 command queue: {:X}", (UINT)result);
            return false;
        }
    }

    // Store dimensions
    _vkDx12SwapchainWidth = width;
    _vkDx12SwapchainHeight = height;
    _vkDx12SwapchainFormat = dxgiFormat;

    LOG_INFO("DX12 interop device and command queue created successfully");
    return true;
}

// hooking
typedef VkResult (*PFN_QueuePresentKHR)(VkQueue, const VkPresentInfoKHR*);
typedef VkResult (*PFN_CreateSwapchainKHR)(VkDevice, const VkSwapchainCreateInfoKHR*, const VkAllocationCallbacks*,
                                           VkSwapchainKHR*);
typedef VkResult (*PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*,
                                                const VkAllocationCallbacks*, VkSurfaceKHR*);

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
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
    std::lock_guard<std::mutex> lock(_vkDeviceMutex);

    if (o_CreateSwapchainKHR != nullptr || State::Instance().vulkanSkipHooks.load())
        return;

    LOG_FUNC();

    o_QueuePresentKHR = (PFN_QueuePresentKHR) (vkGetDeviceProcAddr(InDevice, "vkQueuePresentKHR"));
    o_CreateSwapchainKHR = (PFN_CreateSwapchainKHR) (vkGetDeviceProcAddr(InDevice, "vkCreateSwapchainKHR"));

    if (o_CreateSwapchainKHR != nullptr && o_QueuePresentKHR != nullptr)
    {
        LOG_DEBUG("Hooking VkDevice");

        // Hook
        LONG detourResult = DetourTransactionBegin();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionBegin failed: {}", detourResult);
            return;
        }

        DetourUpdateThread(GetCurrentThread());

        detourResult = DetourAttach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourAttach QueuePresentKHR failed: {}", detourResult);
        }

        detourResult = DetourAttach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourAttach CreateSwapchainKHR failed: {}", detourResult);
        }

        detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit failed: {}", detourResult);
            // Reset pointers on failure to prevent stale state
            o_QueuePresentKHR = nullptr;
            o_CreateSwapchainKHR = nullptr;
        }
    }
    else
    {
        LOG_ERROR("Failed to get Vulkan device proc addresses");
    }
}

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

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks.load() && Config::Instance()->OverlayMenu.value())
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

        if (!szName.empty())
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

    // DX12 interop Frame Generation Present for Vulkan games
    if (State::Instance().currentFG != nullptr && State::Instance().currentFGVk == nullptr)
    {
        // Check if we have FG resources from FfxApi (captured during upscaler dispatch)
        if (FfxApiVk_HasFGResources())
        {
            LOG_TRACE("DX12 interop FG: Found FG resources from Vulkan upscaler");

            // Get dimensions from Vulkan FG resources
            VkImage depthImage = VK_NULL_HANDLE;
            VkImage mvImage = VK_NULL_HANDLE;
            uint32_t width = 0, height = 0;
            FfxApiVk_GetFGResources(&depthImage, &mvImage, &width, &height);

            // Check if resource sharing is initialized
            if (_vkDx12ResourceSharing.IsInitialized())
            {
                // Check if we need to create or recreate shared resources
                uint32_t curWidth = 0, curHeight = 0;
                _vkDx12ResourceSharing.GetDimensions(curWidth, curHeight);

                if (curWidth != width || curHeight != height ||
                    _vkDx12ResourceSharing.GetDepthDx12Resource() == nullptr)
                {
                    LOG_INFO("DX12 interop FG: Creating shared resources {}x{}", width, height);
                    // Create shared resources for depth (D32_SFLOAT) and velocity (RG16_SFLOAT)
                    if (!_vkDx12ResourceSharing.CreateResources(width, height,
                            VK_FORMAT_D32_SFLOAT, VK_FORMAT_R16G16_SFLOAT))
                    {
                        LOG_ERROR("DX12 interop FG: Failed to create shared resources");
                    }
                }

                // Copy Vulkan resources to shared D3D12 resources
                // Note: We need a command buffer for the copy operation
                VkCommandBuffer copyCmd = _vkDx12ResourceSharing.GetCommandBuffer();
                if (copyCmd != VK_NULL_HANDLE && depthImage != VK_NULL_HANDLE && mvImage != VK_NULL_HANDLE)
                {
                    // Begin command buffer
                    VkCommandBufferBeginInfo beginInfo = {};
                    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                    if (vkBeginCommandBuffer(copyCmd, &beginInfo) == VK_SUCCESS)
                    {
                        // Copy depth and velocity to shared resources
                        // Source images are in SHADER_READ_ONLY layout after upscaler dispatch
                        if (_vkDx12ResourceSharing.CopyFromVulkan(copyCmd, depthImage,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                mvImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                width, height))
                        {
                            vkEndCommandBuffer(copyCmd);

                            // Set queue for resource sharing (needed for submit)
                            _vkDx12ResourceSharing.SetQueue(queue, 0); // queueFamilyIndex will be set properly elsewhere

                            // Submit copy command with fence for proper synchronization with timeout
                            if (_vkDx12ResourceSharing.SubmitCopyCommand(copyCmd))
                            {
                                // Wait for copy to complete with timeout (uses vkWaitForFences with 1s timeout)
                                if (_vkDx12ResourceSharing.SynchronizeWithD3D12())
                                {
                                    LOG_DEBUG("DX12 interop FG: Copied Vulkan resources to D3D12");
                                }
                                else
                                {
                                    LOG_ERROR("DX12 interop FG: Synchronization timeout or error");
                                }
                            }
                            else
                            {
                                LOG_ERROR("DX12 interop FG: Failed to submit copy command");
                            }
                        }
                        else
                        {
                            vkEndCommandBuffer(copyCmd);
                            LOG_ERROR("DX12 interop FG: Failed to copy resources");
                        }
                    }
                }

                // Get shared D3D12 resources from resource sharing manager
                ID3D12Resource* sharedDepth = _vkDx12ResourceSharing.GetDepthDx12Resource();
                ID3D12Resource* sharedVelocity = _vkDx12ResourceSharing.GetVelocityDx12Resource();

                // Set depth resource for DX12 FG
                if (sharedDepth != nullptr)
                {
                    Dx12Resource depthRes {};
                    depthRes.type = FG_ResourceType::Depth;
                    depthRes.resource = sharedDepth;
                    depthRes.state = D3D12_RESOURCE_STATE_COMMON;
                    depthRes.width = width;
                    depthRes.height = height;
                    depthRes.validity = FG_ResourceValidity::UntilPresent;
                    State::Instance().currentFG->SetResource(&depthRes);
                    LOG_TRACE("DX12 interop FG: Set depth resource {:X}, {}x{}", (size_t)sharedDepth, width, height);
                }
                else
                {
                    LOG_WARN("DX12 interop FG: Shared depth resource not available");
                }

                // Set velocity resource for DX12 FG
                if (sharedVelocity != nullptr)
                {
                    Dx12Resource velocityRes {};
                    velocityRes.type = FG_ResourceType::Velocity;
                    velocityRes.resource = sharedVelocity;
                    velocityRes.state = D3D12_RESOURCE_STATE_COMMON;
                    velocityRes.width = width;
                    velocityRes.height = height;
                    velocityRes.validity = FG_ResourceValidity::UntilPresent;
                    State::Instance().currentFG->SetResource(&velocityRes);
                    LOG_TRACE("DX12 interop FG: Set velocity resource {:X}, {}x{}", (size_t)sharedVelocity, width, height);
                }
                else
                {
                    LOG_WARN("DX12 interop FG: Shared velocity resource not available");
                }
            }
            else
            {
                LOG_WARN("DX12 interop FG: Resource sharing not initialized");
            }
        }

        // Call DX12 FG Present
        // which is handled by the FfxApi interop layer
        State::Instance().currentFG->Present();
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

        // Initialize Frame Generation for Vulkan games
        // Two paths: (1) Native Vulkan FG (requires FFX VK 3.2+) or (2) DX12 interop FG
        bool useVulkanNativeFG = FfxApiProxy::IsVkFGReady() &&
                                 State::Instance().activeFgInput != FGInput::Upscaler;

        // If native Vulkan FG is not available but user requested FSRFG/FSRFG30 input, warn and suggest alternatives
        if (!useVulkanNativeFG &&
            Config::Instance()->FGEnabled.value_or_default() &&
            (State::Instance().activeFgInput == FGInput::FSRFG || State::Instance().activeFgInput == FGInput::FSRFG30))
        {
            auto version = FfxApiProxy::VersionVk();
            LOG_WARN("===========================================");
            LOG_WARN("Native Vulkan Frame Generation not available");
            LOG_WARN("FFX VK version: {}.{}.{} (requires 3.2+ for native Vulkan FG)", version.major, version.minor, version.patch);
            LOG_WARN("");
            LOG_WARN("For Frame Generation with Vulkan games, use:");
            LOG_WARN("  FGInput = Upscaler (uses DX12 interop)");
            LOG_WARN("===========================================");
        }

        // Initialize native Vulkan FG if FFX VK 3.2+ is available and not using Upscaler FG input
        if (State::Instance().currentFGVk == nullptr && Config::Instance()->FGEnabled.value_or_default() &&
            (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::DLSSG) &&
            useVulkanNativeFG)
        {
            // Check that physical device is available
            if (_PD == VK_NULL_HANDLE)
            {
                LOG_ERROR("Cannot create FSRFG_Vk: physical device not captured");
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

        // DX12 interop FG for Vulkan games when FFX VK < 3.2 or when using Upscaler FG input
        // This creates a D3D12 device and FSRFG_Dx12 for frame generation
        if (!useVulkanNativeFG && State::Instance().currentFG == nullptr &&
            Config::Instance()->FGEnabled.value_or_default() &&
            State::Instance().activeFgInput == FGInput::Upscaler &&
            (State::Instance().activeFgOutput == FGOutput::FSRFG || State::Instance().activeFgOutput == FGOutput::DLSSG))
        {
            auto version = FfxApiProxy::VersionVk();
            LOG_INFO("===========================================");
            LOG_INFO("Vulkan Frame Generation via DX12 interop");
            LOG_INFO("FFX VK version: {}.{}.{}", version.major, version.minor, version.patch);
            LOG_INFO("Using DX12 interop for Frame Generation");
            LOG_INFO("");

            // Check if DX12 FG DLL is available
            if (!FfxApiProxy::IsDx12FGReady())
            {
                // Try to initialize FFX DX12 for FG
                if (!FfxApiProxy::InitFfxDx12())
                {
                    LOG_ERROR("Failed to initialize amd_fidelityfx_dx12.dll for DX12 interop FG");
                    LOG_ERROR("Required: amd_fidelityfx_dx12.dll or amd_fidelityfx_framegeneration_dx12.dll");
                    LOG_INFO("===========================================");
                }
            }

            if (FfxApiProxy::IsDx12FGReady())
            {
                LOG_INFO("DX12 FG API ready, creating D3D12 device and FG swapchain");

                // Create D3D12 device and command queue for interop
                if (CreateVkDx12DeviceAndSwapchain(_hwnd, pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height, pCreateInfo->imageFormat))
                {
                    // Create FSRFG_Dx12 instance
                    auto fg = new FSRFG_Dx12();

                    // Create DXGI factory for swapchain creation
                    IDXGIFactory2* factory = nullptr;
                    HRESULT hr;

                    if (DxgiProxy::Module() == nullptr)
                        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
                    else
                        hr = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

                    if (SUCCEEDED(hr) && factory != nullptr)
                    {
                        // Create hidden window for DX12 FG swapchain if not exists
                        if (_vkDx12HiddenHwnd == nullptr)
                            _vkDx12HiddenHwnd = CreateHiddenWindowForFG();

                        if (_vkDx12HiddenHwnd == nullptr)
                        {
                            LOG_ERROR("Failed to create hidden window for DX12 FG swapchain");
                            factory->Release();
                            delete fg;
                            fg = nullptr;
                            LOG_INFO("===========================================");
                            return result;
                        }

                        // Prepare swapchain description for hidden window
                        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
                        swapChainDesc.BufferDesc.Width = pCreateInfo->imageExtent.width;
                        swapChainDesc.BufferDesc.Height = pCreateInfo->imageExtent.height;
                        swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
                        swapChainDesc.BufferDesc.RefreshRate.Denominator = 0;
                        swapChainDesc.BufferDesc.Format = _vkDx12SwapchainFormat;
                        swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
                        swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
                        swapChainDesc.SampleDesc.Count = 1;
                        swapChainDesc.SampleDesc.Quality = 0;
                        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER;
                        swapChainDesc.BufferCount = pCreateInfo->minImageCount > 2 ? pCreateInfo->minImageCount : 3;
                        swapChainDesc.OutputWindow = _vkDx12HiddenHwnd;  // Use hidden window
                        swapChainDesc.Windowed = TRUE;
                        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                        swapChainDesc.Flags = 0;  // No ALLOW_TEARING for hidden window

                        LOG_DEBUG("Creating FG swapchain: hidden_hwnd={:X}, width={}, height={}, format={}, buffers={}",
                                  (size_t)_vkDx12HiddenHwnd, swapChainDesc.BufferDesc.Width, swapChainDesc.BufferDesc.Height,
                                  (int)swapChainDesc.BufferDesc.Format, swapChainDesc.BufferCount);
                        LOG_DEBUG("DX12 device: {:X}, command queue: {:X}, factory: {:X}",
                                  (size_t)_vkDx12Device, (size_t)_vkDx12CommandQueue, (size_t)factory);

                        // Create FG swapchain via FSRFG_Dx12
                        IDXGISwapChain* fgSwapchain = nullptr;

                        // Skip DXGI load checks during FG swapchain creation
                        {
                            ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

                            if (fg->CreateSwapchain(factory, _vkDx12CommandQueue, &swapChainDesc, &fgSwapchain))
                            {
                                // Store swapchain in state BEFORE CreateContext (needed for FG context creation)
                                State::Instance().currentFG = fg;
                                State::Instance().currentFGSwapchain = fgSwapchain;
                                State::Instance().currentSwapchain = fgSwapchain;
                                State::Instance().currentD3D12Device = _vkDx12Device;
                                State::Instance().currentCommandQueue = _vkDx12CommandQueue;

                                // Store swapchain info
                                _vkDx12Swapchain = fgSwapchain;

                                // Store Vulkan device info for resource capture
                                State::Instance().currentVkDevice = device;
                                State::Instance().currentVkPhysicalDevice = _PD;
                                State::Instance().currentVkSwapchain = *pSwapchain;

                                // Create FG context (needs currentSwapchain to be set)
                                FG_Constants fgConstants {};
                                fgConstants.displayWidth = pCreateInfo->imageExtent.width;
                                fgConstants.displayHeight = pCreateInfo->imageExtent.height;

                                fg->CreateContext(_vkDx12Device, fgConstants);
                                fg->Activate();

                                // Initialize Vulkan-DX12 resource sharing for FG
                                if (_vkDx12ResourceSharing.Init(device, _PD, _vkDx12Device, vkGetDeviceProcAddr))
                                {
                                    LOG_INFO("Vulkan-DX12 resource sharing initialized for FG");
                                }
                                else
                                {
                                    LOG_WARN("Failed to initialize Vulkan-DX12 resource sharing, FG may not work");
                                }

                                LOG_INFO("FSRFG_Dx12 initialized for Vulkan via DX12 interop");
                                LOG_INFO("FG swapchain created: {:X}", (size_t)fgSwapchain);
                            }
                            else
                            {
                                LOG_ERROR("Failed to create FSRFG_Dx12 swapchain");
                                delete fg;
                                fg = nullptr;
                            }
                        }

                        factory->Release();
                    }
                    else
                    {
                        LOG_ERROR("Failed to create DXGI factory for FG swapchain");
                        delete fg;
                        fg = nullptr;
                    }
                }
                else
                {
                    LOG_ERROR("Failed to create D3D12 device for DX12 interop FG");
                }

                LOG_INFO("===========================================");
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

    // Cleanup DX12 interop resources
    if (_vkDx12Swapchain != nullptr)
    {
        _vkDx12Swapchain->Release();
        _vkDx12Swapchain = nullptr;
    }
    if (_vkDx12CommandQueue != nullptr)
    {
        _vkDx12CommandQueue->Release();
        _vkDx12CommandQueue = nullptr;
    }
    if (_vkDx12Device != nullptr)
    {
        _vkDx12Device->Release();
        _vkDx12Device = nullptr;
    }
    if (_vkDx12HiddenHwnd != nullptr)
    {
        DestroyWindow(_vkDx12HiddenHwnd);
        _vkDx12HiddenHwnd = nullptr;
    }

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_QueuePresentKHR != nullptr)
    {
        DetourDetach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);
        o_QueuePresentKHR = nullptr;
    }

    if (o_CreateSwapchainKHR != nullptr)
    {
        DetourDetach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);
        o_CreateSwapchainKHR = nullptr;
    }

    if (o_vkCreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);
        o_vkCreateDevice = nullptr;
    }

    if (o_vkCreateInstance != nullptr)
    {
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);
        o_vkCreateInstance = nullptr;
    }

    if (o_vkCreateWin32SurfaceKHR != nullptr)
    {
        DetourDetach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);
        o_vkCreateWin32SurfaceKHR = nullptr;
    }

    if (o_vkGetInstanceProcAddr != nullptr)
    {
        DetourDetach(&(PVOID&) o_vkGetInstanceProcAddr, hkvkGetInstanceProcAddr);
        o_vkGetInstanceProcAddr = nullptr;
    }

    if (o_vkGetDeviceProcAddr != nullptr)
    {
        DetourDetach(&(PVOID&) o_vkGetDeviceProcAddr, hkvkGetDeviceProcAddr);
        o_vkGetDeviceProcAddr = nullptr;
    }

    DetourTransactionCommit();
}
