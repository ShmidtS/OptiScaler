#include "pch.h"
#include "FG_Hooks.h"
#include <Config.h>

#include <framegen/ffx/FSRFG_Dx12.h>
#include <framegen/xefg/XeFG_Dx12.h>

#include <inputs/FG/FSR3_Dx12_FG.h>
#include <inputs/FG/FfxApi_Dx12_FG.h>

#include <hudfix/Hudfix_Dx12.h>
#include <resource_tracking/ResTrack_Dx12.h>

#include <misc/FrameLimit.h>
#include <upscaler_time/UpscalerTime_Dx12.h>

#include <detours/detours.h>

#include <d3d12.h>

// Thread-local skip flags for recursive call protection
thread_local bool FGHooks::_skipResize = false;
thread_local bool FGHooks::_skipResize1 = false;
thread_local bool FGHooks::_skipPresent = false;
thread_local bool FGHooks::_skipPresent1 = false;

static bool CheckForFGStatus()
{
    // Need to check overlay menu parameter, goes to places it shouldn't go
    // if (!Config::Instance()->OverlayMenu.value_or_default())
    //    return false;

    if (State::Instance().activeFgInput == FGInput::NoFG || State::Instance().activeFgInput == FGInput::Nukems)
        return false;

    // Disable FG if amd dll is not found
    if (State::Instance().activeFgOutput == FGOutput::FSRFG)
    {
        FfxApiProxy::InitFfxDx12();
        if (!FfxApiProxy::IsFGReady())
        {
            LOG_DEBUG("Can't init FfxApiProxy, disabling FGOutput");
            Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
            State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
        }
    }
    else if (State::Instance().activeFgOutput == FGOutput::XeFG && !XeFGProxy::InitXeFG())
    {
        LOG_DEBUG("Can't init XeFGProxy, disabling FGOutput");
        Config::Instance()->FGOutput.set_volatile_value(FGOutput::NoFG);
        State::Instance().activeFgOutput = Config::Instance()->FGOutput.value_or_default();
    }

    if (State::Instance().activeFgOutput != FGOutput::FSRFG && State::Instance().activeFgOutput != FGOutput::XeFG)
    {
        LOG_WARN("FGOutput is not set to FSR-FG or XeFG");
        return false;
    }

    return true;
}

// Helper: Get or create FG instance, returns true if a new instance was created
// Returns nullptr on failure, existing or new FG instance on success
static IFGFeature_Dx12* GetOrCreateFGInstance(bool& outCreatedNew)
{
    outCreatedNew = false;

    if (State::Instance().currentFG == nullptr)
    {
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            State::Instance().currentFG = new FSRFG_Dx12();
        }
        else if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            State::Instance().currentFG =
                new XeFG_Dx12(Config::Instance()->FGXeFGInterpolationCount.value_or_default());
        }
        outCreatedNew = true;
    }
    else
    {
        // Check if FG type changed
        bool typeChanged = false;
        if (State::Instance().activeFgOutput == FGOutput::FSRFG &&
            dynamic_cast<FSRFG_Dx12*>(State::Instance().currentFG) == nullptr)
            typeChanged = true;
        else if (State::Instance().activeFgOutput == FGOutput::XeFG &&
                 dynamic_cast<XeFG_Dx12*>(State::Instance().currentFG) == nullptr)
            typeChanged = true;

        if (typeChanged)
        {
            LOG_INFO("FG type changed, cleaning up old instance");
            IFGFeature_Dx12* oldFG = State::Instance().currentFG;
            State::Instance().currentFG = nullptr;
            oldFG->Shutdown();
            delete oldFG;

            if (State::Instance().activeFgOutput == FGOutput::FSRFG)
                State::Instance().currentFG = new FSRFG_Dx12();
            else if (State::Instance().activeFgOutput == FGOutput::XeFG)
                State::Instance().currentFG =
                    new XeFG_Dx12(Config::Instance()->FGXeFGInterpolationCount.value_or_default());
            outCreatedNew = true;
        }
    }

    return State::Instance().currentFG;
}

// Helper: Cleanup FG instance on swapchain creation failure
static void CleanupFGOnFailure(bool wasNewlyCreated)
{
    if (wasNewlyCreated && State::Instance().currentFG != nullptr)
    {
        LOG_ERROR("Swapchain creation failed, cleaning up FG instance");
        IFGFeature_Dx12* failedFG = State::Instance().currentFG;
        State::Instance().currentFG = nullptr;
        failedFG->Shutdown();
        delete failedFG;
    }
}

// Helper: Fix unsupported swap effects for DX12
static void FixSwapEffectForDX12(DXGI_SWAP_EFFECT& swapEffect)
{
    if (swapEffect == DXGI_SWAP_EFFECT_SEQUENTIAL)
    {
        LOG_WARN("DXGI_SWAP_EFFECT_SEQUENTIAL is not supported in DX12, changing to FLIP_SEQUENTIAL");
        swapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    }
    else if (swapEffect == DXGI_SWAP_EFFECT_DISCARD)
    {
        LOG_WARN("DXGI_SWAP_EFFECT_DISCARD is not supported in DX12, changing to FLIP_DISCARD");
        swapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    }
}

// ResizeBuffers parameters for common processing
struct ResizeBuffersParams
{
    UINT BufferCount;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT SwapChainFlags;
};

// Helper: Check if resize can be skipped for XeFG
static bool CheckSkipResizeForXeFG(IDXGISwapChain* This, ResizeBuffersParams& params)
{
    if (State::Instance().activeFgOutput != FGOutput::XeFG || State::Instance().SCExclusiveFullscreen)
        return false;

    if (!Config::Instance()->FGXeFGSkipResizeBuffers.value_or_default())
        return false;

    DXGI_SWAP_CHAIN_DESC desc {};
    if (This->GetDesc(&desc) != S_OK)
        return false;

    LOG_DEBUG("SC BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {}",
              desc.BufferCount, desc.BufferDesc.Width, desc.BufferDesc.Height,
              (UINT)desc.BufferDesc.Format, State::Instance().SCLastFlags);

    if (params.BufferCount == 0)
        params.BufferCount = desc.BufferCount;

    if ((desc.BufferDesc.Width == params.Width || params.Width == 0) &&
        (desc.BufferDesc.Height == params.Height || params.Height == 0) &&
        (params.Format == desc.BufferDesc.Format || params.Format == 0) &&
        State::Instance().SCLastFlags == params.SwapChainFlags &&
        (params.BufferCount == desc.BufferCount || params.BufferCount == 0))
    {
        return true;
    }

    return false;
}

// Helper: Modify buffer state and swapchain index for XeFG
static void ModifyBufferStateAndIndex(IDXGISwapChain* This, IFGFeature_Dx12* fg, UINT bufferCount)
{
    if (!Config::Instance()->FGXeFGModifyBufferState.value_or_default() &&
        !Config::Instance()->FGXeFGModifySCIndex.value_or_default())
        return;

    auto swapchain = reinterpret_cast<IDXGISwapChain3*>(This);
    auto swapchainIndex = swapchain->GetCurrentBackBufferIndex();

    if (fg != nullptr && Config::Instance()->FGXeFGModifyBufferState.value_or_default())
    {
        LOG_INFO("Trying to change backbuffer state to COMMON");
        auto cmdList = fg->GetUICommandList();

        if (cmdList != nullptr)
        {
            for (size_t i = 0; i < bufferCount; i++)
            {
                ID3D12Resource* backBuffer = nullptr;
                if (swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
                {
                    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                        backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
                    cmdList->ResourceBarrier(1, &barrier);
                    backBuffer->Release();
                }
            }
        }
    }

    if (swapchainIndex != 0 && Config::Instance()->FGXeFGModifySCIndex.value_or_default())
    {
        auto presents = bufferCount - swapchainIndex;
        LOG_DEBUG("Trying to reset backbuffer index: {} with {} present calls", swapchainIndex, presents);
        for (size_t i = 0; i < presents; i++)
        {
            swapchain->Present(0, 0);
        }
    }
}

// Helper: Apply XeFG-specific flag modifications
static void ApplyXeFGFlagModifications(UINT& SwapChainFlags)
{
    if (State::Instance().activeFgOutput != FGOutput::XeFG)
        return;

    if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        SwapChainFlags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    }

    if (State::Instance().SCLastFlags != SwapChainFlags)
    {
        LOG_WARN("SwapChainFlags changed from {} to {}", State::Instance().SCLastFlags, SwapChainFlags);
        if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            LOG_WARN("Preventing flag change for XeFG!");
            SwapChainFlags = State::Instance().SCLastFlags;
        }
    }
}

// Helper: Post-resize window adjustment for borderless mode
static void ApplyBorderlessResize(HWND hwnd)
{
    if (!Config::Instance()->FGXeFGForceBorderless.value_or_default() || !State::Instance().SCExclusiveFullscreen)
        return;

    SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

    Util::MonitorInfo info = Util::GetMonitorInfoForWindow(hwnd);
    LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}",
              info.width, info.height, info.x, info.y, wstring_to_string(info.name));
    SetWindowPos(hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

HRESULT FGHooks::CreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                 IDXGISwapChain** ppSwapChain)
{
    if (!CheckForFGStatus())
    {
        LOG_WARN("Can't init FG Feature or invalid FGOutput setting!");
        return E_NOINTERFACE;
    }

    if (pDesc == nullptr || ppSwapChain == nullptr)
    {
        LOG_ERROR("Invalid parameters: pDesc or ppSwapChain is nullptr");
        return E_INVALIDARG;
    }

    // Check if it's Dx12
    ID3D12CommandQueue* cq = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
    {
        LOG_ERROR("FG Feature requires D3D12 Command Queue!");
        return E_INVALIDARG;
    }

    // Get or create FG instance
    bool createdNewFG = false;
    auto fg = GetOrCreateFGInstance(createdNewFG);
    if (fg == nullptr)
    {
        cq->Release();
        LOG_ERROR("Failed to create FG instance");
        return E_FAIL;
    }

    bool scResult = false;
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        if (State::Instance().activeFgOutput == FGOutput::XeFG && !pDesc->Windowed)
            LOG_WARN("Using exclusive fullscreen with XeFG!!!");

        FixSwapEffectForDX12(pDesc->SwapEffect);

        scResult = fg->CreateSwapchain(pFactory, cq, pDesc, ppSwapChain);

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    cq->Release();

    if (scResult && *ppSwapChain != nullptr)
    {
        _hwnd = pDesc->OutputWindow;
        State::Instance().currentFGSwapchain = *ppSwapChain;
        HookFGSwapchain(*ppSwapChain);
        State::Instance().currentSwapchain = *ppSwapChain;
        return S_OK;
    }

    CleanupFGOnFailure(createdNewFG);
    return E_FAIL;
}

HRESULT FGHooks::CreateSwapChainForHwnd(IDXGIFactory* pFactory, IUnknown* pDevice, HWND hWnd,
                                        DXGI_SWAP_CHAIN_DESC1* pDesc, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                        IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
    if (!CheckForFGStatus())
    {
        LOG_WARN("Can't init FG Feature or invalid FGOutput setting!");
        return E_NOINTERFACE;
    }

    if (pDesc == nullptr || ppSwapChain == nullptr)
    {
        LOG_ERROR("Invalid parameters: pDesc or ppSwapChain is nullptr");
        return E_INVALIDARG;
    }

    // Check if it's Dx12
    ID3D12CommandQueue* cq = nullptr;
    if (pDevice->QueryInterface(IID_PPV_ARGS(&cq)) != S_OK)
    {
        LOG_ERROR("FG Feature requires D3D12 Command Queue!");
        return E_INVALIDARG;
    }

    // Get or create FG instance
    bool createdNewFG = false;
    auto fg = GetOrCreateFGInstance(createdNewFG);
    if (fg == nullptr)
    {
        cq->Release();
        LOG_ERROR("Failed to create FG instance");
        return E_FAIL;
    }

    bool scResult = false;
    {
        ScopedSkipDxgiLoadChecks skipDxgiLoadChecks {};

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = true;

        if (State::Instance().activeFgOutput == FGOutput::XeFG && pFullscreenDesc != nullptr &&
            !pFullscreenDesc->Windowed)
            LOG_WARN("Using exclusive fullscreen with XeFG!!!");

        FixSwapEffectForDX12(pDesc->SwapEffect);

        scResult = fg->CreateSwapchain1(pFactory, cq, hWnd, pDesc, pFullscreenDesc, ppSwapChain);

        if (Config::Instance()->FGDontUseSwapchainBuffers.value_or_default())
            State::Instance().skipHeapCapture = false;
    }

    cq->Release();

    if (scResult && *ppSwapChain != nullptr)
    {
        _hwnd = hWnd;
        State::Instance().currentFGSwapchain = *ppSwapChain;
        HookFGSwapchain(*ppSwapChain);
        State::Instance().currentSwapchain = *ppSwapChain;
        return S_OK;
    }

    CleanupFGOnFailure(createdNewFG);
    return E_FAIL;
}

void FGHooks::HookFGSwapchain(IDXGISwapChain* pSwapChain)
{
    // Use static mutex for thread safety
    static std::mutex fgHookMutex;
    std::lock_guard<std::mutex> lock(fgHookMutex);

    if (o_FGSCPresent != nullptr || pSwapChain == nullptr)
        return;

    void** pFactoryVTable = *reinterpret_cast<void***>(pSwapChain);
    if (pFactoryVTable == nullptr)
    {
        LOG_ERROR("VTable pointer is null in HookFGSwapchain");
        return;
    }

    o_FGRelease = (PFN_Release) pFactoryVTable[2];
    o_FGSCPresent = (PFN_Present) pFactoryVTable[8];
    o_FGSCSetFullscreenState = (PFN_SetFullscreenState) pFactoryVTable[10];
    o_FGSCGetFullscreenState = (PFN_GetFullscreenState) pFactoryVTable[11];
    o_FGSCResizeBuffers = (PFN_ResizeBuffers) pFactoryVTable[13];
    o_FGSCResizeTarget = (PFN_ResizeTarget) pFactoryVTable[14];
    o_FGSCGetFullscreenDesc = (PFN_GetFullscreenDesc) pFactoryVTable[19];
    o_FGSCPresent1 = (PFN_Present1) pFactoryVTable[22];
    o_FGSCResizeBuffers1 = (PFN_ResizeBuffers1) pFactoryVTable[39];

    if (o_FGSCPresent != nullptr)
    {
        LOG_INFO("Hooking FG SwapChain present");
        LOG_TRACE("FGRelease: {:X}", (size_t) o_FGRelease);
        LOG_TRACE("FGSCPresent: {:X}", (size_t) o_FGSCPresent);
        LOG_TRACE("FGSCSetFullscreenState: {:X}", (size_t) o_FGSCSetFullscreenState);
        LOG_TRACE("FGSCGetFullscreenState: {:X}", (size_t) o_FGSCGetFullscreenState);
        LOG_TRACE("FGSCResizeBuffers: {:X}", (size_t) o_FGSCResizeBuffers);
        LOG_TRACE("FGSCResizeTarget: {:X}", (size_t) o_FGSCResizeTarget);
        LOG_TRACE("FGSCGetFullscreenDesc: {:X}", (size_t) o_FGSCGetFullscreenDesc);
        LOG_TRACE("FGSCPresent1: {:X}", (size_t) o_FGSCPresent1);
        LOG_TRACE("FGSCResizeBuffers1: {:X}", (size_t) o_FGSCResizeBuffers1);

        LONG detourResult = DetourTransactionBegin();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionBegin failed in HookFGSwapchain: {}", detourResult);
            return;
        }

        DetourUpdateThread(GetCurrentThread());

        detourResult = DetourAttach(&(PVOID&) o_FGRelease, hkFGRelease);
        if (detourResult != NO_ERROR)
            LOG_ERROR("DetourAttach o_FGRelease failed: {}", detourResult);

        detourResult = DetourAttach(&(PVOID&) o_FGSCPresent, hkFGPresent);
        if (detourResult != NO_ERROR)
            LOG_ERROR("DetourAttach o_FGSCPresent failed: {}", detourResult);

        detourResult = DetourAttach(&(PVOID&) o_FGSCResizeTarget, hkResizeTarget);
        if (detourResult != NO_ERROR)
            LOG_ERROR("DetourAttach o_FGSCResizeTarget failed: {}", detourResult);

        detourResult = DetourAttach(&(PVOID&) o_FGSCResizeBuffers, hkResizeBuffers);
        if (detourResult != NO_ERROR)
            LOG_ERROR("DetourAttach o_FGSCResizeBuffers failed: {}", detourResult);

        detourResult = DetourAttach(&(PVOID&) o_FGSCSetFullscreenState, hkSetFullscreenState);
        if (detourResult != NO_ERROR)
            LOG_ERROR("DetourAttach o_FGSCSetFullscreenState failed: {}", detourResult);

        if (o_FGSCPresent1 != nullptr)
        {
            detourResult = DetourAttach(&(PVOID&) o_FGSCPresent1, hkFGPresent1);
            if (detourResult != NO_ERROR)
                LOG_ERROR("DetourAttach o_FGSCPresent1 failed: {}", detourResult);
        }

        if (o_FGSCResizeBuffers1 != nullptr)
        {
            detourResult = DetourAttach(&(PVOID&) o_FGSCResizeBuffers1, hkResizeBuffers1);
            if (detourResult != NO_ERROR)
                LOG_ERROR("DetourAttach o_FGSCResizeBuffers1 failed: {}", detourResult);
        }

        if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            detourResult = DetourAttach(&(PVOID&) o_FGSCGetFullscreenState, hkGetFullscreenState);
            if (detourResult != NO_ERROR)
                LOG_ERROR("DetourAttach o_FGSCGetFullscreenState failed: {}", detourResult);

            if (o_FGSCGetFullscreenDesc != nullptr)
            {
                detourResult = DetourAttach(&(PVOID&) o_FGSCGetFullscreenDesc, hkGetFullscreenDesc);
                if (detourResult != NO_ERROR)
                    LOG_ERROR("DetourAttach o_FGSCGetFullscreenDesc failed: {}", detourResult);
            }
        }

        detourResult = DetourTransactionCommit();
        if (detourResult != NO_ERROR)
        {
            LOG_ERROR("DetourTransactionCommit failed in HookFGSwapchain: {}", detourResult);
        }
    }
}

HRESULT FGHooks::hkSetFullscreenState(IDXGISwapChain* This, BOOL Fullscreen, IDXGIOutput* pTarget)
{
    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().FGchanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    bool modeChanged = false;
    if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        if (Fullscreen)
        {
            Fullscreen = false;

            if (!State::Instance().SCExclusiveFullscreen)
            {
                State::Instance().SCExclusiveFullscreen = true;
                modeChanged = true;
            }

            LOG_DEBUG("Prevented exclusive fullscreen");
        }
        else
        {
            if (State::Instance().SCExclusiveFullscreen)
            {
                modeChanged = true;
                State::Instance().SCExclusiveFullscreen = false;
            }
        }
    }

    State::Instance().realExclusiveFullscreen = Fullscreen;

    if (State::Instance().activeFgOutput == FGOutput::XeFG && Fullscreen)
        LOG_WARN("Using exclusive fullscreen with XeFG!!!");

    auto result = S_OK;

    if (!Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        result = o_FGSCSetFullscreenState(This, Fullscreen, pTarget);
        LOG_DEBUG("Fullscreen: {}, Result: {:X}", Fullscreen, (UINT) result);
    }

    if (result == S_OK && modeChanged)
    {
        LOG_DEBUG("Mode changed");

        DXGI_SWAP_CHAIN_DESC scDesc {};
        This->GetDesc(&scDesc);

        if (State::Instance().SCExclusiveFullscreen)
        {
            SetWindowLongPtr(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);

            Util::MonitorInfo info;

            if (pTarget != nullptr)
                info = Util::GetMonitorInfoForOutput(pTarget);
            else
                info = Util::GetMonitorInfoForWindow(_hwnd);

            LOG_DEBUG("Overriding window size: {}x{}, and pos: {}x{} at monitor: {}", info.width, info.height, info.x,
                      info.y, wstring_to_string(info.name));

            SetWindowPos(_hwnd, HWND_TOP, info.x, info.y, info.width, info.height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
        else
        {
            SetWindowLongPtr(_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
            SetWindowLongPtr(_hwnd, GWL_EXSTYLE, WS_EX_OVERLAPPEDWINDOW);
            SetWindowPos(_hwnd, nullptr, 0, 0, scDesc.BufferDesc.Width, scDesc.BufferDesc.Height,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    return result;
}

HRESULT FGHooks::hkGetFullscreenDesc(IDXGISwapChain* This, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    auto result = o_FGSCGetFullscreenDesc(This, pDesc);

    if (result == S_OK && State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        pDesc->Windowed = false;
    }

    return result;
}

HRESULT FGHooks::hkGetFullscreenState(IDXGISwapChain* This, BOOL* pFullscreen, IDXGIOutput** ppTarget)
{
    auto result = o_FGSCGetFullscreenState(This, pFullscreen, ppTarget);

    if (result == S_OK && State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        *pFullscreen = true;
    }

    return result;
}

HRESULT FGHooks::hkResizeBuffers(IDXGISwapChain* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                 UINT SwapChainFlags)
{
    // Skip XeFG's internal call
    if (_skipResize)
    {
        LOG_DEBUG("XeFG call skipping");
        _skipResize = false;
        return o_FGSCResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    ApplyXeFGFlagModifications(SwapChainFlags);

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {}", BufferCount, Width, Height,
              (UINT) NewFormat, SwapChainFlags);

    auto fg = State::Instance().currentFG;

    // Check if resize can be skipped
    ResizeBuffersParams params { BufferCount, Width, Height, NewFormat, SwapChainFlags };
    if (CheckSkipResizeForXeFG(This, params))
    {
        LOG_DEBUG("Skipping resize");
        ModifyBufferStateAndIndex(This, fg, params.BufferCount);
        return S_OK;
    }

    // Add ALLOW_TEARING for non-exclusive fullscreen XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG && !State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGSkipResizeBuffers.value_or_default())
    {
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = SwapChainFlags;

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().FGchanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    _skipResize1 = true;

    HRESULT result;
    {
        ScopedSkipSpoofing skipSpoofing {};
        result = o_FGSCResizeBuffers(This, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    _skipResize1 = false;

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    if (result == S_OK)
    {
        if (fg != nullptr)
        {
            State::Instance().FGchanged = true;
            fg->Deactivate();
            fg->UpdateTarget();
        }
        ApplyBorderlessResize(_hwnd);
    }

    return result;
}

HRESULT FGHooks::hkResizeTarget(IDXGISwapChain* This, DXGI_MODE_DESC* pNewTargetParameters)
{
    if (Config::Instance()->FGXeFGForceBorderless.value_or_default())
    {
        LOG_DEBUG("Skipping resize target.");
        return S_OK;
    }

    auto fg = State::Instance().currentFG;
    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().FGchanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    return o_FGSCResizeTarget(This, pNewTargetParameters);
}

HRESULT FGHooks::hkResizeBuffers1(IDXGISwapChain* This, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                  UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
    // Skip XeFG's internal call
    if (_skipResize1)
    {
        LOG_DEBUG("XeFG call skipping");
        _skipResize1 = false;
        return o_FGSCResizeBuffers1(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                    ppPresentQueue);
    }

    ApplyXeFGFlagModifications(SwapChainFlags);

    LOG_DEBUG("BufferCount: {}, Width: {}, Height: {}, NewFormat:{}, SwapChainFlags: {}, Caller: {}", BufferCount,
              Width, Height, (UINT) Format, SwapChainFlags, Util::WhoIsTheCaller(_ReturnAddress()));

    auto fg = State::Instance().currentFG;

    // Check if resize can be skipped
    ResizeBuffersParams params { BufferCount, Width, Height, Format, SwapChainFlags };
    if (CheckSkipResizeForXeFG(This, params))
    {
        LOG_DEBUG("Skipping resize");
        ModifyBufferStateAndIndex(This, fg, params.BufferCount);
        return S_OK;
    }

    // Add ALLOW_TEARING for non-exclusive fullscreen XeFG
    if (State::Instance().activeFgOutput == FGOutput::XeFG && !State::Instance().SCExclusiveFullscreen &&
        Config::Instance()->FGXeFGSkipResizeBuffers.value_or_default())
    {
        SwapChainFlags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }

    State::Instance().SCAllowTearing = (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) > 0;
    State::Instance().SCLastFlags = SwapChainFlags;

    if (fg != nullptr && fg->IsActive())
    {
        State::Instance().FGchanged = true;
        fg->UpdateTarget();
        fg->Deactivate();
    }

    HRESULT result;
    {
        ScopedSkipSpoofing skipSpoofing {};
        _skipResize = true;
        result = o_FGSCResizeBuffers1(This, BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask,
                                      ppPresentQueue);
        _skipResize = false;
    }

    LOG_DEBUG("Result: {:X}, Caller: {}", (UINT) result, Util::WhoIsTheCaller(_ReturnAddress()));

    if (result == S_OK)
    {
        if (fg != nullptr)
        {
            State::Instance().FGchanged = true;
            fg->Deactivate();
            fg->UpdateTarget();
        }
        ApplyBorderlessResize(_hwnd);
    }

    return result;
}

HRESULT FGHooks::hkFGPresent(void* This, UINT SyncInterval, UINT Flags)
{
    // Skip XeFG's internal call
    if (_skipPresent)
    {
        LOG_DEBUG("XeFG call skipping");
        return o_FGSCPresent(This, SyncInterval, Flags);
    }

    LOG_DEBUG("SyncInterval: {}, Flags: {:X}", SyncInterval, Flags);

    _skipPresent1 = true;
    auto result = FGPresent(This, SyncInterval, Flags, nullptr);
    _skipPresent1 = false;

    return result;
}

HRESULT FGHooks::hkFGPresent1(void* This, UINT SyncInterval, UINT Flags,
                              const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    // Skip XeFG's internal call
    if (_skipPresent1)
    {
        LOG_DEBUG("XeFG call skipping");
        return o_FGSCPresent1(This, SyncInterval, Flags, pPresentParameters);
    }

    LOG_DEBUG("SyncInterval: {}, Flags: {:X}", SyncInterval, Flags);
    _skipPresent = true;
    auto result = FGPresent(This, SyncInterval, Flags, pPresentParameters);
    _skipPresent = false;

    return result;
}

HRESULT FGHooks::FGPresent(void* This, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    _lastPresentFlags = Flags;

    if (State::Instance().isShuttingDown)
    {
        if (pPresentParameters == nullptr)
            return o_FGSCPresent(This, SyncInterval, Flags);
        else
            return o_FGSCPresent1(This, SyncInterval, Flags, pPresentParameters);
    }

    auto willPresent = (Flags & DXGI_PRESENT_TEST) == 0;

    if (willPresent)
    {
        State::Instance().FGLastFrame++;

        double ftDelta = 0.0f;
        auto now = Util::MillisecondsNow();

        if (_lastFGFrameTime != 0)
            ftDelta = now - _lastFGFrameTime;

        _lastFGFrameTime = now;
        State::Instance().lastFGFrameTime = ftDelta;

        LOG_DEBUG("flags: {:X}, Frametime: {}", Flags, ftDelta);
    }

    if (willPresent && State::Instance().currentCommandQueue != nullptr)
    {
        UpscalerTimeDx12::ReadUpscalingTime(State::Instance().currentCommandQueue);
    }

    auto fg = State::Instance().currentFG;

    // RAII mutex guard for exception safety
    struct MutexGuard {
        IFGFeature_Dx12* fg;
        bool locked;
        MutexGuard(IFGFeature_Dx12* f) : fg(f), locked(false) {}
        ~MutexGuard() { if (locked && fg) { fg->Mutex.unlockThis(2); } }
        void lock() { if (fg) { fg->Mutex.lock(2); locked = true; } }
    } mutexGuard(fg);

    if (willPresent && fg != nullptr && fg->IsActive() &&
        Config::Instance()->FGUseMutexForSwapchain.value_or_default() && fg->Mutex.getOwner() != 2)
    {
        LOG_TRACE("Waiting FG->Mutex 2, current: {}", fg->Mutex.getOwner());
        mutexGuard.lock();
        LOG_TRACE("Accuired FG->Mutex: {}", fg->Mutex.getOwner());
    }

    if (willPresent && fg != nullptr)
    {
        // Some games use this callback to render UI even when
        // FG is disabled. So call it when there is FGFeature
        if (State::Instance().activeFgInput == FGInput::FSRFG)
            ffxPresentCallback();
        else if (State::Instance().activeFgInput == FGInput::FSRFG30)
            FSR3FG::ffxPresentCallback();

        // And if Optiscalers FG is active call
        // FG Features present
        fg->Present();
    }

    if (willPresent)
    {
        ResTrack_Dx12::ClearPossibleHudless();
        Hudfix_Dx12::PresentStart();
    }

    if (willPresent && fg != nullptr && fg->IsUsingUI() && Config::Instance()->FGDrawUIOverFG.value_or_default())
    {
        ID3D12Resource* backBuffer = nullptr;
        auto swapchain = ((IDXGISwapChain3*) This);
        auto swapchainIndex = swapchain->GetCurrentBackBufferIndex();

        if (swapchain->GetBuffer(swapchainIndex, IID_PPV_ARGS(&backBuffer)) == S_OK)
        {
            auto result = fg->GetResourceCopy(FG_ResourceType::HudlessColor, D3D12_RESOURCE_STATE_PRESENT, backBuffer);
            backBuffer->Release();

            if (!result)
                LOG_WARN("Couldn't copy hudless into the backbuffer");
        }
    }

    if (willPresent && Config::Instance()->ForceVsync.has_value())
    {
        LOG_DEBUG("ForceVsync: {}, VsyncInterval: {}, SCAllowTearing: {}, realExclusiveFullscreen: {}",
                  Config::Instance()->ForceVsync.value(), Config::Instance()->VsyncInterval.value_or_default(),
                  State::Instance().SCAllowTearing, State::Instance().realExclusiveFullscreen);

        if (!Config::Instance()->ForceVsync.value())
        {
            SyncInterval = 0;

            if (State::Instance().SCAllowTearing && !State::Instance().realExclusiveFullscreen)
            {
                LOG_DEBUG("Adding DXGI_PRESENT_ALLOW_TEARING");
                Flags |= DXGI_PRESENT_ALLOW_TEARING;
            }
        }
        else
        {
            SyncInterval = Config::Instance()->VsyncInterval.value_or_default();

            if (SyncInterval < 1)
                SyncInterval = 1;

            LOG_DEBUG("Removing DXGI_PRESENT_ALLOW_TEARING");
            Flags &= ~DXGI_PRESENT_ALLOW_TEARING;
        }

        LOG_DEBUG("Final SyncInterval: {}", SyncInterval);
    }

    // Used at wrapped_swapchain LocalPresent to determine is frame is interpolated or not
    if (willPresent)
        State::Instance().FGPresentIsCalled = true;

    HRESULT result;
    if (pPresentParameters == nullptr)
        result = o_FGSCPresent(This, SyncInterval, Flags);
    else
        result = o_FGSCPresent1(This, SyncInterval, Flags, pPresentParameters);
    LOG_DEBUG("Result: {:X}", result);

    Hudfix_Dx12::PresentEnd();

    if (willPresent && !State::Instance().reflexLimitsFps && State::Instance().activeFgOutput != FGOutput::NoFG)
        FrameLimit::sleep(fg != nullptr ? fg->IsActive() : false);

    // Mutex automatically released by RAII guard

    return result;
}

HRESULT FGHooks::hkFGRelease(IDXGISwapChain* This)
{
    if (State::Instance().currentFGSwapchain != This || State::Instance().isShuttingDown)
        return o_FGRelease(This);

    // AddRef to check refcount without releasing
    This->AddRef();
    ULONG refCount = o_FGRelease(This);

    if (refCount == 1)
    {
        LOG_INFO("Preserving FG Swapchain from release");
        return refCount;  // Return actual refcount to indicate preserved state
    }

    return refCount;  // Return actual refcount
}
