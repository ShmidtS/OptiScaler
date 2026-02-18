#pragma once

#include <hooks/Reflex_Hooks.h>

#include "NvApiTypes.h"
#include "fakenvapi.h"
#include <mutex>
#include <atomic>

class NvApiHooks
{
  private:
    inline static std::mutex _hookMutex;
    inline static std::atomic<bool> _isHooked{false};

  public:
    // Note: These cannot be atomic as DetourAttach/DetourDetach require PVOID& reference
    inline static PFN_NvApi_QueryInterface o_NvAPI_QueryInterface = nullptr;
    inline static decltype(&NvAPI_GPU_GetArchInfo) o_NvAPI_GPU_GetArchInfo = nullptr;
    inline static decltype(&NvAPI_DRS_GetSetting) o_NvAPI_DRS_GetSetting = nullptr;

    static NvAPI_Status __stdcall hkNvAPI_GPU_GetArchInfo(NvPhysicalGpuHandle hPhysicalGpu,
                                                          NV_GPU_ARCH_INFO* pGpuArchInfo);
    static NvAPI_Status __stdcall hkNvAPI_DRS_GetSetting(NvDRSSessionHandle hSession, NvDRSProfileHandle hProfile,
                                                         NvU32 settingId, NVDRS_SETTING* pSetting);
    static void* __stdcall hkNvAPI_QueryInterface(unsigned int InterfaceId);
    static void Hook(HMODULE nvapiModule);
    static void Unhook();
};
