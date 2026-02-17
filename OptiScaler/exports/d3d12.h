#pragma once

#include "SysUtils.h"
#include "shared.h"

#include <proxies/D3D12_Proxy.h>
#include <proxies/KernelBase_Proxy.h>
#include <atomic>
#include <shared_mutex>

struct d3d12_dll
{
    HMODULE dll = nullptr;
    std::atomic<bool> initialized{false};
    std::shared_mutex loadMutex;

    FARPROC GetBehaviorValue = nullptr;
    FARPROC SetAppCompatStringPointer = nullptr;
    FARPROC D3D12CoreCreateLayeredDevice = nullptr;
    FARPROC D3D12CoreGetLayeredDeviceSize = nullptr;
    FARPROC D3D12CoreRegisterLayers = nullptr;
    FARPROC D3D12DeviceRemovedExtendedData = nullptr;
    FARPROC D3D12PIXEventsReplaceBlock = nullptr;
    FARPROC D3D12PIXGetThreadInfo = nullptr;
    FARPROC D3D12PIXNotifyWakeFromFenceSignal = nullptr;
    FARPROC D3D12PIXReportCounter = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        std::unique_lock<std::shared_mutex> lock(loadMutex);

        dll = module;

        GetBehaviorValue = KernelBaseProxy::GetProcAddress_()(dll, "GetBehaviorValue");
        SetAppCompatStringPointer = KernelBaseProxy::GetProcAddress_()(dll, "SetAppCompatStringPointer");
        D3D12CoreCreateLayeredDevice = KernelBaseProxy::GetProcAddress_()(dll, "D3D12CoreCreateLayeredDevice");
        D3D12CoreGetLayeredDeviceSize = KernelBaseProxy::GetProcAddress_()(dll, "D3D12CoreGetLayeredDeviceSize");
        D3D12CoreRegisterLayers = KernelBaseProxy::GetProcAddress_()(dll, "D3D12CoreRegisterLayers");
        D3D12DeviceRemovedExtendedData = KernelBaseProxy::GetProcAddress_()(dll, "D3D12DeviceRemovedExtendedData");
        D3D12PIXEventsReplaceBlock = KernelBaseProxy::GetProcAddress_()(dll, "D3D12PIXEventsReplaceBlock");
        D3D12PIXGetThreadInfo = KernelBaseProxy::GetProcAddress_()(dll, "D3D12PIXGetThreadInfo");
        D3D12PIXNotifyWakeFromFenceSignal =
            KernelBaseProxy::GetProcAddress_()(dll, "D3D12PIXNotifyWakeFromFenceSignal");
        D3D12PIXReportCounter = KernelBaseProxy::GetProcAddress_()(dll, "D3D12PIXReportCounter");

        initialized.store(true, std::memory_order_release);
    }

    bool IsInitialized() const { return initialized.load(std::memory_order_acquire); }
} d3d12;

// Export functions with proper calling convention and safety checks
HRESULT WINAPI _D3D12CreateDeviceExport(IUnknown* adapter, D3D_FEATURE_LEVEL minLevel, REFIID riid, void** ppDevice)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12CreateDevice_Hooked()(adapter, minLevel, riid, ppDevice);
}

HRESULT WINAPI _D3D12SerializeRootSignatureExport(D3d12Proxy::D3D12_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                           D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob,
                                           ID3DBlob** ppErrorBlob)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12SerializeRootSignature_Hooked()(pRootSignature, Version, ppBlob, ppErrorBlob);
}

HRESULT WINAPI _D3D12CreateRootSignatureDeserializerExport(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
                                                    REFIID pRootSignatureDeserializerInterface,
                                                    void** ppRootSignatureDeserializer)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12CreateRootSignatureDeserializer_Hooked()(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);
}

HRESULT WINAPI _D3D12SerializeVersionedRootSignatureExport(D3d12Proxy::D3D12_VERSIONED_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                                    ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12SerializeVersionedRootSignature_Hooked()(pRootSignature, ppBlob, ppErrorBlob);
}

HRESULT WINAPI _D3D12CreateVersionedRootSignatureDeserializerExport(LPCVOID pSrcData, SIZE_T SrcDataSizeInBytes,
                                                             REFIID pRootSignatureDeserializerInterface,
                                                             void** ppRootSignatureDeserializer)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12CreateVersionedRootSignatureDeserializer_Hooked()(
        pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface, ppRootSignatureDeserializer);
}

HRESULT WINAPI _D3D12GetDebugInterfaceExport(REFIID riid, void** ppDebug)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12GetDebugInterface_Hooked()(riid, ppDebug);
}

HRESULT WINAPI _D3D12EnableExperimentalFeaturesExport(UINT NumFeatures, const IID* pIIDs, void* pConfigurationStructs,
                                               UINT* pConfigurationStructSizes)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12EnableExperimentalFeatures_Hooked()(NumFeatures, pIIDs, pConfigurationStructs,
                                                                pConfigurationStructSizes);
}

HRESULT WINAPI _D3D12GetInterfaceExport(REFCLSID clsid, REFIID riid, void** ppInterface)
{
    LOG_FUNC();
    return D3d12Proxy::D3D12GetInterface_Hooked()(clsid, riid, ppInterface);
}

void WINAPI _GetBehaviorValue()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_GetBehaviorValue called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.GetBehaviorValue);
}

void WINAPI _D3D12CoreCreateLayeredDevice()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12CoreCreateLayeredDevice called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12CoreCreateLayeredDevice);
}

void WINAPI _D3D12CoreGetLayeredDeviceSize()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12CoreGetLayeredDeviceSize called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12CoreGetLayeredDeviceSize);
}

void WINAPI _D3D12CoreRegisterLayers()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12CoreRegisterLayers called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12CoreRegisterLayers);
}

void WINAPI _D3D12DeviceRemovedExtendedData()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12DeviceRemovedExtendedData called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12DeviceRemovedExtendedData);
}

void WINAPI _D3D12PIXEventsReplaceBlock()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12PIXEventsReplaceBlock called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12PIXEventsReplaceBlock);
}

void WINAPI _D3D12PIXGetThreadInfo()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12PIXGetThreadInfo called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12PIXGetThreadInfo);
}

void WINAPI _D3D12PIXNotifyWakeFromFenceSignal()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12PIXNotifyWakeFromFenceSignal called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12PIXNotifyWakeFromFenceSignal);
}

void WINAPI _D3D12PIXReportCounter()
{
    LOG_FUNC();
    if (!d3d12.IsInitialized())
    {
        LOG_WARN("_D3D12PIXReportCounter called before initialization");
        return;
    }
    SafeCall_VOID(d3d12.D3D12PIXReportCounter);
}