#pragma once

#include "SysUtils.h"

#include "shared.h"

#include <proxies/KernelBase_Proxy.h>
#include <atomic>
#include <shared_mutex>

struct version_dll
{
    HMODULE dll = nullptr;
    std::atomic<bool> initialized{false};
    std::shared_mutex loadMutex;

    FARPROC GetFileVersionInfoA = nullptr;
    FARPROC GetFileVersionInfoByHandle = nullptr;
    FARPROC GetFileVersionInfoExA = nullptr;
    FARPROC GetFileVersionInfoExW = nullptr;
    FARPROC GetFileVersionInfoSizeA = nullptr;
    FARPROC GetFileVersionInfoSizeExA = nullptr;
    FARPROC GetFileVersionInfoSizeExW = nullptr;
    FARPROC GetFileVersionInfoSizeW = nullptr;
    FARPROC GetFileVersionInfoW = nullptr;
    FARPROC VerFindFileA = nullptr;
    FARPROC VerFindFileW = nullptr;
    FARPROC VerInstallFileA = nullptr;
    FARPROC VerInstallFileW = nullptr;
    FARPROC VerLanguageNameA = nullptr;
    FARPROC VerLanguageNameW = nullptr;
    FARPROC VerQueryValueA = nullptr;
    FARPROC VerQueryValueW = nullptr;

    void LoadOriginalLibrary(HMODULE module)
    {
        std::unique_lock<std::shared_mutex> lock(loadMutex);

        dll = module;
        shared.LoadOriginalLibrary(dll);

        GetFileVersionInfoA = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoA");
        GetFileVersionInfoByHandle = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoByHandle");
        GetFileVersionInfoExA = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoExA");
        GetFileVersionInfoExW = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoExW");
        GetFileVersionInfoSizeA = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoSizeA");
        GetFileVersionInfoSizeExA = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoSizeExA");
        GetFileVersionInfoSizeExW = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoSizeExW");
        GetFileVersionInfoSizeW = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoSizeW");
        GetFileVersionInfoW = KernelBaseProxy::GetProcAddress_()(dll, "GetFileVersionInfoW");
        VerFindFileA = KernelBaseProxy::GetProcAddress_()(dll, "VerFindFileA");
        VerFindFileW = KernelBaseProxy::GetProcAddress_()(dll, "VerFindFileW");
        VerInstallFileA = KernelBaseProxy::GetProcAddress_()(dll, "VerInstallFileA");
        VerInstallFileW = KernelBaseProxy::GetProcAddress_()(dll, "VerInstallFileW");
        VerLanguageNameA = KernelBaseProxy::GetProcAddress_()(dll, "VerLanguageNameA");
        VerLanguageNameW = KernelBaseProxy::GetProcAddress_()(dll, "VerLanguageNameW");
        VerQueryValueA = KernelBaseProxy::GetProcAddress_()(dll, "VerQueryValueA");
        VerQueryValueW = KernelBaseProxy::GetProcAddress_()(dll, "VerQueryValueW");

        initialized.store(true, std::memory_order_release);
    }

    bool IsInitialized() const { return initialized.load(std::memory_order_acquire); }
} version;

// Export functions with proper calling convention and safety checks
void WINAPI _GetFileVersionInfoA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoA called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoA);
}

void WINAPI _GetFileVersionInfoByHandle()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoByHandle called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoByHandle);
}

void WINAPI _GetFileVersionInfoExA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoExA called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoExA);
}

void WINAPI _GetFileVersionInfoExW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoExW called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoExW);
}

void WINAPI _GetFileVersionInfoSizeA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoSizeA called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoSizeA);
}

void WINAPI _GetFileVersionInfoSizeExA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoSizeExA called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoSizeExA);
}

void WINAPI _GetFileVersionInfoSizeExW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoSizeExW called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoSizeExW);
}

void WINAPI _GetFileVersionInfoSizeW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoSizeW called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoSizeW);
}

void WINAPI _GetFileVersionInfoW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_GetFileVersionInfoW called before initialization");
        return;
    }
    SafeCall_VOID(version.GetFileVersionInfoW);
}

void WINAPI _VerFindFileA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerFindFileA called before initialization");
        return;
    }
    SafeCall_VOID(version.VerFindFileA);
}

void WINAPI _VerFindFileW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerFindFileW called before initialization");
        return;
    }
    SafeCall_VOID(version.VerFindFileW);
}

void WINAPI _VerInstallFileA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerInstallFileA called before initialization");
        return;
    }
    SafeCall_VOID(version.VerInstallFileA);
}

void WINAPI _VerInstallFileW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerInstallFileW called before initialization");
        return;
    }
    SafeCall_VOID(version.VerInstallFileW);
}

void WINAPI _VerLanguageNameA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerLanguageNameA called before initialization");
        return;
    }
    SafeCall_VOID(version.VerLanguageNameA);
}

void WINAPI _VerLanguageNameW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerLanguageNameW called before initialization");
        return;
    }
    SafeCall_VOID(version.VerLanguageNameW);
}

void WINAPI _VerQueryValueA()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerQueryValueA called before initialization");
        return;
    }
    SafeCall_VOID(version.VerQueryValueA);
}

void WINAPI _VerQueryValueW()
{
    if (!version.IsInitialized())
    {
        LOG_WARN("_VerQueryValueW called before initialization");
        return;
    }
    SafeCall_VOID(version.VerQueryValueW);
}