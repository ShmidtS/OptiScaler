#pragma once
#include "SysUtils.h"
#include "FG_Hooks.h"
#include <dxgi1_6.h>
#include <atomic>

class DxgiFactoryHooks
{
  public:
    static void HookToFactory(IDXGIFactory* pFactory);

  private:
    typedef HRESULT (*PFN_EnumAdapterByGpuPreference)(IDXGIFactory6* This, UINT Adapter,
                                                      DXGI_GPU_PREFERENCE GpuPreference, REFIID riid,
                                                      IUnknown** ppvAdapter);
    typedef HRESULT (*PFN_EnumAdapterByLuid)(IDXGIFactory4* This, LUID AdapterLuid, REFIID riid, IUnknown** ppvAdapter);
    typedef HRESULT (*PFN_EnumAdapters1)(IDXGIFactory1* This, UINT Adapter, IUnknown** ppAdapter);
    typedef HRESULT (*PFN_EnumAdapters)(IDXGIFactory* This, UINT Adapter, IUnknown** ppAdapter);
    typedef HRESULT (*PFN_CreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
    typedef HRESULT (*PFN_CreateSwapChainForHwnd)(IDXGIFactory*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                  IDXGISwapChain1**);
    typedef HRESULT (*PFN_CreateSwapChainForCoreWindow)(IDXGIFactory2*, IUnknown* pDevice, IUnknown* pWindow,
                                                        DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                        IDXGISwapChain1** ppSwapChain);

    inline static PFN_EnumAdapterByGpuPreference o_EnumAdapterByGpuPreference = nullptr;
    inline static PFN_EnumAdapterByLuid o_EnumAdapterByLuid = nullptr;
    inline static PFN_EnumAdapters1 o_EnumAdapters1 = nullptr;
    inline static PFN_EnumAdapters o_EnumAdapters = nullptr;
    inline static PFN_CreateSwapChain o_CreateSwapChain = nullptr;
    inline static PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;
    inline static PFN_CreateSwapChainForCoreWindow o_CreateSwapChainForCoreWindow = nullptr;

    static HRESULT CreateSwapChain(IDXGIFactory* realFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                   IDXGISwapChain** ppSwapChain);

    static HRESULT CreateSwapChainForHwnd(IDXGIFactory2* realFactory, IUnknown* pDevice, HWND hWnd,
                                          DXGI_SWAP_CHAIN_DESC1* pDesc,
                                          DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                          IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain);

    static HRESULT CreateSwapChainForCoreWindow(IDXGIFactory2* realFactory, IUnknown* pDevice, IUnknown* pWindow,
                                                DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput,
                                                IDXGISwapChain1** ppSwapChain);

    static HRESULT EnumAdapters(IDXGIFactory* realFactory, UINT Adapter, IDXGIAdapter** ppAdapter);
    static HRESULT EnumAdapters1(IDXGIFactory1* realFactory, UINT Adapter, IDXGIAdapter1** ppAdapter);
    static HRESULT EnumAdapterByLuid(IDXGIFactory4* realFactory, LUID AdapterLuid, REFIID riid, void** ppvAdapter);
    static HRESULT EnumAdapterByGpuPreference(IDXGIFactory6* realFactory, UINT Adapter,
                                              DXGI_GPU_PREFERENCE GpuPreference, REFIID riid, void** ppvAdapter);

    // To prevent recursive FG swapchain creation
    // Using atomic for thread safety - these are accessed from multiple threads
    inline static std::atomic<bool> _skipFGSwapChainCreation{false};
    inline static std::atomic<bool> _skipHighPerfCheck{false};

    static void CheckAdapter(IUnknown* unkAdapter);

    class ScopedSkipFGSCCreation
    {
      private:
        bool previousState;

      public:
        ScopedSkipFGSCCreation()
        {
            previousState = DxgiFactoryHooks::_skipFGSwapChainCreation.load(std::memory_order_relaxed);
            DxgiFactoryHooks::_skipFGSwapChainCreation.store(true, std::memory_order_relaxed);
        }
        ~ScopedSkipFGSCCreation()
        {
            DxgiFactoryHooks::_skipFGSwapChainCreation.store(previousState, std::memory_order_relaxed);
        }
    };

    class ScopedSkipHighPerfCheck
    {
      private:
        bool previousState;

      public:
        ScopedSkipHighPerfCheck()
        {
            previousState = DxgiFactoryHooks::_skipHighPerfCheck.load(std::memory_order_relaxed);
            DxgiFactoryHooks::_skipHighPerfCheck.store(true, std::memory_order_relaxed);
        }
        ~ScopedSkipHighPerfCheck()
        {
            DxgiFactoryHooks::_skipHighPerfCheck.store(previousState, std::memory_order_relaxed);
        }
    };
};
