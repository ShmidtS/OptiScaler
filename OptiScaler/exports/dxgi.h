#pragma once

#include "SysUtils.h"

#include <proxies/Dxgi_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <atomic>
#include <shared_mutex>

struct dxgi_dll
{
    HMODULE dll = nullptr;
    std::atomic<bool> initialized{false};
    std::shared_mutex loadMutex;

    FARPROC ApplyCompatResolutionQuirking = nullptr;
    FARPROC CompatString = nullptr;
    FARPROC CompatValue = nullptr;
    FARPROC D3D10CreateDevice = nullptr;
    FARPROC D3D10CreateLayeredDevice = nullptr;
    FARPROC D3D10GetLayeredDeviceSize = nullptr;
    FARPROC D3D10RegisterLayers = nullptr;
    FARPROC D3D10ETWRundown = nullptr;
    FARPROC DumpJournal = nullptr;
    FARPROC ReportAdapterConfiguration = nullptr;
    FARPROC PIXBeginCapture = nullptr;
    FARPROC PIXEndCapture = nullptr;
    FARPROC PIXGetCaptureState = nullptr;
    FARPROC SetAppCompatStringPointer = nullptr;
    FARPROC UpdateHMDEmulationStatus = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        std::unique_lock<std::shared_mutex> lock(loadMutex);

        dll = module;

        ApplyCompatResolutionQuirking = KernelBaseProxy::GetProcAddress_()(module, "ApplyCompatResolutionQuirking");
        CompatString = KernelBaseProxy::GetProcAddress_()(module, "CompatString");
        CompatValue = KernelBaseProxy::GetProcAddress_()(module, "CompatValue");
        D3D10CreateDevice = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateDevice");
        D3D10CreateLayeredDevice = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10CreateLayeredDevice");
        D3D10GetLayeredDeviceSize = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10GetLayeredDeviceSize");
        D3D10RegisterLayers = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10RegisterLayers");
        D3D10ETWRundown = KernelBaseProxy::GetProcAddress_()(module, "DXGID3D10ETWRundown");
        DumpJournal = KernelBaseProxy::GetProcAddress_()(module, "DXGIDumpJournal");
        ReportAdapterConfiguration = KernelBaseProxy::GetProcAddress_()(module, "DXGIReportAdapterConfiguration");
        PIXBeginCapture = KernelBaseProxy::GetProcAddress_()(module, "PIXBeginCapture");
        PIXEndCapture = KernelBaseProxy::GetProcAddress_()(module, "PIXEndCapture");
        PIXGetCaptureState = KernelBaseProxy::GetProcAddress_()(module, "PIXGetCaptureState");
        SetAppCompatStringPointer = KernelBaseProxy::GetProcAddress_()(module, "SetAppCompatStringPointer");
        UpdateHMDEmulationStatus = KernelBaseProxy::GetProcAddress_()(module, "UpdateHMDEmulationStatus");

        initialized.store(true, std::memory_order_release);
    }

    bool IsInitialized() const { return initialized.load(std::memory_order_acquire); }
} dxgi;

// Export functions with proper calling convention and safety checks
HRESULT WINAPI _CreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    LOG_FUNC();
    return DxgiProxy::CreateDxgiFactory_()(riid, ppFactory);
}

HRESULT WINAPI _CreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    LOG_FUNC();
    return DxgiProxy::CreateDxgiFactory1_()(riid, ppFactory);
}

HRESULT WINAPI _CreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    LOG_FUNC();
    return DxgiProxy::CreateDxgiFactory2_Hooked()(Flags, riid, ppFactory);
}

HRESULT WINAPI _DXGIDeclareAdapterRemovalSupport()
{
    LOG_FUNC();
    return DxgiProxy::DeclareAdepterRemovalSupport_()();
}

HRESULT WINAPI _DXGIGetDebugInterface1(UINT Flags, REFIID riid, void** pDebug)
{
    LOG_FUNC();
    return DxgiProxy::GetDebugInterface_()(Flags, riid, pDebug);
}

void WINAPI _ApplyCompatResolutionQuirking()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_ApplyCompatResolutionQuirking called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.ApplyCompatResolutionQuirking);
}

void WINAPI _CompatString()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_CompatString called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.CompatString);
}

void WINAPI _CompatValue()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_CompatValue called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.CompatValue);
}

void WINAPI _DXGID3D10CreateDevice()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGID3D10CreateDevice called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.D3D10CreateDevice);
}

void WINAPI _DXGID3D10CreateLayeredDevice()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGID3D10CreateLayeredDevice called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.D3D10CreateLayeredDevice);
}

void WINAPI _DXGID3D10GetLayeredDeviceSize()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGID3D10GetLayeredDeviceSize called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.D3D10GetLayeredDeviceSize);
}

void WINAPI _DXGID3D10RegisterLayers()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGID3D10RegisterLayers called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.D3D10RegisterLayers);
}

void WINAPI _DXGID3D10ETWRundown()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGID3D10ETWRundown called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.D3D10ETWRundown);
}

void WINAPI _DXGIDumpJournal()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGIDumpJournal called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.DumpJournal);
}

void WINAPI _DXGIReportAdapterConfiguration()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_DXGIReportAdapterConfiguration called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.ReportAdapterConfiguration);
}

void WINAPI _PIXBeginCapture()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_PIXBeginCapture called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.PIXBeginCapture);
}

void WINAPI _PIXEndCapture()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_PIXEndCapture called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.PIXEndCapture);
}

void WINAPI _PIXGetCaptureState()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_PIXGetCaptureState called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.PIXGetCaptureState);
}

void WINAPI _SetAppCompatStringPointer()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_SetAppCompatStringPointer called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.SetAppCompatStringPointer);
}

void WINAPI _UpdateHMDEmulationStatus()
{
    LOG_FUNC();
    if (!dxgi.IsInitialized())
    {
        LOG_WARN("_UpdateHMDEmulationStatus called before initialization");
        return;
    }
    SafeCall_VOID(dxgi.UpdateHMDEmulationStatus);
}