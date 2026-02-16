#pragma once
#include "SysUtils.h"

#include <filesystem>

#include <dxgi.h>
#include <xess.h>

// Common constants
constexpr size_t MAX_IMAGE_OFFSET = 0x1000000;    // Maximum valid e_lfanew offset in PE header
constexpr size_t FRAME_TIMES_SIZE = 300;          // Number of frame time entries to track
constexpr unsigned int DEFAULT_WIDTH = 1920;      // Default display width
constexpr unsigned int DEFAULT_HEIGHT = 1080;     // Default display height
constexpr unsigned int SIZE_IN_BYTES_MULTIPLIER = 31; // SizeInBytes = Width * Height * multiplier

// DLSS preset counts
constexpr size_t DLSS_PRESET_COUNT = 17;
constexpr size_t DLSSD_PRESET_COUNT = 6;

// Mipmap bias limits
constexpr float MIPMAP_BIAS_MAX = 15.0f;
constexpr float MIPMAP_BIAS_MIN = -15.0f;

namespace Util
{
typedef struct _version_t
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} version_t;

struct MonitorInfo
{
    HMONITOR handle;
    int x;
    int y;
    int width;
    int height;
    RECT monitorRect;  // full monitor bounds
    RECT workRect;     // work area (taskbar excluded)
    std::wstring name; // e.g., \\.\DISPLAY1
};

std::filesystem::path ExePath();
std::filesystem::path DllPath();
std::optional<std::filesystem::path> NvngxPath();
double MillisecondsNow();

HWND GetProcessWindow();

// GetDLLVersion: Extracts file version from a DLL.
// Returns true on success, false on failure.
// WARNING: Caller must check return value before using versionOut.
bool GetDLLVersion(std::wstring dllPath, version_t* versionOut);
bool GetDLLVersion(std::wstring dllPath, xess_version_t* versionOut);
bool GetRealWindowsVersion(OSVERSIONINFOW& osInfo);
std::string GetWindowsName(const OSVERSIONINFOW& os);
std::wstring GetExeProductName();
std::wstring GetWindowTitle(HWND hwnd);
std::optional<std::filesystem::path> FindFilePath(const std::filesystem::path& startDir,
                                                  const std::filesystem::path fileName);
std::string WhoIsTheCaller(void* returnAddress);
HMODULE GetCallerModule(void* returnAddress);
MonitorInfo GetMonitorInfoForWindow(HWND hwnd);
MonitorInfo GetMonitorInfoForOutput(IDXGIOutput* pOutput);
int GetActiveRefreshRate(HWND hwnd);
bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);

}; // namespace Util

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch DirectX API errors
        throw std::exception(std::to_string(hr).c_str());
    }
}
