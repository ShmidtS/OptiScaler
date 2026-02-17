#pragma once

#include "SysUtils.h"

#include <proxies/KernelBase_Proxy.h>
#include <atomic>
#include <shared_mutex>

struct shared
{
    HMODULE dll = nullptr;
    std::atomic<bool> initialized{false};
    std::shared_mutex loadMutex;

    FARPROC DllCanUnloadNow = nullptr;
    FARPROC DllGetClassObject = nullptr;
    FARPROC DllRegisterServer = nullptr;
    FARPROC DllUnregisterServer = nullptr;
    FARPROC DebugSetMute = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        std::unique_lock<std::shared_mutex> lock(loadMutex);

        dll = module;

        DllCanUnloadNow = KernelBaseProxy::GetProcAddress_()(module, "DllCanUnloadNow");
        DllGetClassObject = KernelBaseProxy::GetProcAddress_()(module, "DllGetClassObject");
        DllRegisterServer = KernelBaseProxy::GetProcAddress_()(module, "DllRegisterServer");
        DllUnregisterServer = KernelBaseProxy::GetProcAddress_()(module, "DllUnregisterServer");
        DebugSetMute = KernelBaseProxy::GetProcAddress_()(module, "DebugSetMute");

        initialized.store(true, std::memory_order_release);
    }

    bool IsInitialized() const { return initialized.load(std::memory_order_acquire); }
} shared;

// Safe call wrapper for FARPROC
template<typename... Args>
inline HRESULT SafeCall_FARPROC(FARPROC proc, Args... args)
{
    if (proc == nullptr)
    {
        LOG_WARN("Export function called but original function pointer is NULL");
        return E_FAIL;
    }
    return proc(args...);
}

inline void SafeCall_VOID(FARPROC proc)
{
    if (proc == nullptr)
    {
        LOG_WARN("Export function called but original function pointer is NULL");
        return;
    }
    proc();
}

// Export functions with proper calling convention and safety checks
HRESULT WINAPI _DllCanUnloadNow()
{
    if (!shared.IsInitialized())
    {
        LOG_WARN("_DllCanUnloadNow called before initialization");
        return E_FAIL;
    }
    return SafeCall_FARPROC(shared.DllCanUnloadNow);
}

HRESULT WINAPI _DllGetClassObject()
{
    if (!shared.IsInitialized())
    {
        LOG_WARN("_DllGetClassObject called before initialization");
        return E_FAIL;
    }
    return SafeCall_FARPROC(shared.DllGetClassObject);
}

HRESULT WINAPI _DllRegisterServer()
{
    if (!shared.IsInitialized())
    {
        LOG_WARN("_DllRegisterServer called before initialization");
        return E_FAIL;
    }
    return SafeCall_FARPROC(shared.DllRegisterServer);
}

HRESULT WINAPI _DllUnregisterServer()
{
    if (!shared.IsInitialized())
    {
        LOG_WARN("_DllUnregisterServer called before initialization");
        return E_FAIL;
    }
    return SafeCall_FARPROC(shared.DllUnregisterServer);
}

void WINAPI _DebugSetMute()
{
    if (!shared.IsInitialized())
    {
        LOG_WARN("_DebugSetMute called before initialization");
        return;
    }
    SafeCall_VOID(shared.DebugSetMute);
}