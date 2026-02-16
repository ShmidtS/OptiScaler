#include "pch.h"

#include "Config.h"

#include "Util.h"

#include "nvapi/fakenvapi.h"
#include <hooks/Streamline_Hooks.h>

#include <SimpleIni.h>
#include <mutex>

static CSimpleIniA ini;
static CSimpleIniA fakenvapiIni;
static std::mutex iniMutex;
static std::mutex fakenvapiIniMutex;

static inline int64_t GetTicks()
{
    LARGE_INTEGER ticks;

    if (!QueryPerformanceCounter(&ticks))
        return 0;

    return ticks.QuadPart;
}

static inline bool isInteger(const std::string& str, int& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

static inline bool isUInt(const std::string& str, uint32_t& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

static inline bool isFloat(const std::string& str, float& value)
{
    std::istringstream iss(str);
    return (iss >> value) && iss.eof();
}

Config::Config()
{
    absoluteFileName = Util::DllPath().parent_path() / fileName;
    Reload(absoluteFileName);
}

bool Config::Reload(std::filesystem::path iniPath)
{
    std::lock_guard<std::mutex> lock(iniMutex);
    auto pathWStr = iniPath.wstring();

    LOG_INFO("Trying to load ini from: {0}", wstring_to_string(pathWStr));
    if (ini.LoadFile(iniPath.c_str()) == SI_OK)
    {
        State::Instance().nvngxIniDetected = exists(iniPath.parent_path() / "nvngx.ini");
        _log.clear();

        // Upscalers
        {
            Dx11Upscaler.set_from_config(readString("Upscalers", "Dx11Upscaler", true));
            Dx12Upscaler.set_from_config(readString("Upscalers", "Dx12Upscaler", true));
            VulkanUpscaler.set_from_config(readString("Upscalers", "VulkanUpscaler", true));
        }

        // Frame Generation
        {
            FGEnabled.set_from_config(readBool("FrameGen", "Enabled"));
            FGDebugView.set_from_config(readBool("FrameGen", "DebugView"));

            if (auto FGInputString = readString("FrameGen", "FGInput"); FGInputString.has_value())
            {
                if (lstrcmpiA(FGInputString.value().c_str(), "nofg") == 0)
                    FGInput.set_from_config(FGInput::NoFG);
                else if (lstrcmpiA(FGInputString.value().c_str(), "upscaler") == 0)
                    FGInput.set_from_config(FGInput::Upscaler);
                else if (lstrcmpiA(FGInputString.value().c_str(), "nukems") == 0)
                {
                    FGInput.set_from_config(FGInput::Nukems);
                    FGOutput.set_from_config(FGOutput::Nukems);
                }
                else if (lstrcmpiA(FGInputString.value().c_str(), "dlssg") == 0)
                    FGInput.set_from_config(FGInput::DLSSG);
                else if (lstrcmpiA(FGInputString.value().c_str(), "fsrfg") == 0)
                    FGInput.set_from_config(FGInput::FSRFG);
                else if (lstrcmpiA(FGInputString.value().c_str(), "fsrfg30") == 0)
                    FGInput.set_from_config(FGInput::FSRFG30);
            }

            if (auto FGOutputString = readString("FrameGen", "FGOutput");
                FGInput.value_or_default() != FGInput::Nukems && FGOutputString.has_value())
            {
                if (lstrcmpiA(FGOutputString.value().c_str(), "nofg") == 0)
                    FGOutput.set_from_config(FGOutput::NoFG);
                else if (lstrcmpiA(FGOutputString.value().c_str(), "fsrfg") == 0)
                    FGOutput.set_from_config(FGOutput::FSRFG);
                else if (lstrcmpiA(FGOutputString.value().c_str(), "nukems") == 0)
                    FGOutput.set_from_config(FGOutput::Nukems);
                else if (lstrcmpiA(FGOutputString.value().c_str(), "xefg") == 0)
                    FGOutput.set_from_config(FGOutput::XeFG);
            }

            FGDrawUIOverFG.set_from_config(readBool("FrameGen", "DrawUIOverFG"));
            FGUIPremultipliedAlpha.set_from_config(readBool("FrameGen", "UIPremultipliedAlpha"));
            FGDisableHudless.set_from_config(readBool("FrameGen", "DisableHudless"));
            FGDisableUI.set_from_config(readBool("FrameGen", "DisableUI"));
            FGSkipReset.set_from_config(readBool("FrameGen", "SkipReset"));
            FGRectLeft.set_from_config(readInt("FrameGen", "RectLeft"));
            FGRectTop.set_from_config(readInt("FrameGen", "RectTop"));
            FGRectWidth.set_from_config(readInt("FrameGen", "RectWidth"));
            FGRectHeight.set_from_config(readInt("FrameGen", "RectHeight"));

            FGAllowedFrameAhead.set_from_config(readInt("FrameGen", "AllowedFrameAhead"));
            if (FGAllowedFrameAhead.has_value() && (FGAllowedFrameAhead.value() < 1 || FGAllowedFrameAhead.value() > 3))
                FGAllowedFrameAhead.reset();

            FGDepthValidNow.set_from_config(readBool("FrameGen", "DepthValidNow"));
            FGVelocityValidNow.set_from_config(readBool("FrameGen", "VelocityValidNow"));
            FGHudlessValidNow.set_from_config(readBool("FrameGen", "HudlessValidNow"));
            FGOnlyAcceptFirstHudless.set_from_config(readBool("FrameGen", "OnlyAcceptFirstHudless"));
        }

        // FSR FG
        {
            FGDebugTearLines.set_from_config(readBool("FSRFG", "DebugTearLines"));
            FGDebugResetLines.set_from_config(readBool("FSRFG", "DebugResetLines"));
            FGDebugPacingLines.set_from_config(readBool("FSRFG", "DebugPacingLines"));
            FGAsync.set_from_config(readBool("FSRFG", "AllowAsync"));
            FGUseMutexForSwapchain.set_from_config(readBool("FSRFG", "UseMutexForSwapchain"));
            FGFramePacingTuning.set_from_config(readBool("FSRFG", "FramePacingTuning"));
            FGFPTSafetyMarginInMs.set_from_config(readFloat("FSRFG", "FPTSafetyMarginInMs"));
            FGFPTVarianceFactor.set_from_config(readFloat("FSRFG", "FPTVarianceFactor"));
            FGFPTAllowHybridSpin.set_from_config(readBool("FSRFG", "FPTHybridSpin"));
            FGFPTHybridSpinTime.set_from_config(readInt("FSRFG", "FPTHybridSpinTime"));
            FGFPTAllowWaitForSingleObjectOnFence.set_from_config(readInt("FSRFG", "FPTWaitForSingleObjectOnFence"));
            FSRFGEnableWatermark.set_from_config(readBool("FSRFG", "EnableWatermark"));
        }

        // OptiFG
        {
            FGHUDFix.set_from_config(readBool("OptiFG", "HUDFix"));
            FGHUDLimit.set_from_config(readInt("OptiFG", "HUDLimit"));
            FGHUDFixExtended.set_from_config(readBool("OptiFG", "HUDFixExtended"));
            FGImmediateCapture.set_from_config(readBool("OptiFG", "HUDFixImmediate"));
            FGUseShards.set_from_config(readBool("OptiFG", "UseShards"));
            FGAlwaysTrackHeaps.set_from_config(readBool("OptiFG", "AlwaysTrackHeaps"));
            FGResourceBlocking.set_from_config(readBool("OptiFG", "ResourceBlocking"));
            FGMakeDepthCopy.set_from_config(readBool("OptiFG", "MakeDepthCopy"));
            FGMakeMVCopy.set_from_config(readBool("OptiFG", "MakeMVCopy"));
            FGHudfixDisableRTV.set_from_config(readBool("OptiFG", "HudfixDisableRTV"));
            FGHudfixDisableSRV.set_from_config(readBool("OptiFG", "HudfixDisableSRV"));
            FGHudfixDisableUAV.set_from_config(readBool("OptiFG", "HudfixDisableUAV"));
            FGHudfixDisableOM.set_from_config(readBool("OptiFG", "HudfixDisableOM"));
            FGHudfixDisableDispatch.set_from_config(readBool("OptiFG", "HudfixDisableDispatch"));
            FGHudfixDisableDI.set_from_config(readBool("OptiFG", "HudfixDisableDI"));
            FGHudfixDisableDII.set_from_config(readBool("OptiFG", "HudfixDisableDII"));
            FGHudfixDisableSCR.set_from_config(readBool("OptiFG", "HudfixDisableSCR"));
            FGHudfixDisableSGR.set_from_config(readBool("OptiFG", "HudfixDisableSGR"));

            FGEnableDepthScale.set_from_config(readBool("OptiFG", "EnableDepthScale"));
            FGDepthScaleMax.set_from_config(readFloat("OptiFG", "DepthScaleMax"));

            FGDontUseSwapchainBuffers.set_from_config(readBool("OptiFG", "HUDFixDontUseSwapchainBuffers"));
            FGRelaxedResolutionCheck.set_from_config(readBool("OptiFG", "HUDFixRelaxedResolutionCheck"));

            FGResourceFlip.set_from_config(readBool("OptiFG", "ResourceFlip"));
            FGResourceFlipOffset.set_from_config(readBool("OptiFG", "ResourceFlipOffset"));

            FGAlwaysCaptureFSRFGSwapchain.set_from_config(readBool("OptiFG", "AlwaysCaptureFSRFGSwapchain"));
        }

        {
            FGXeFGInterpolationCount.set_from_config(readInt("XeFG", "InterpolationCount"));
            if (FGXeFGInterpolationCount.has_value() &&
                (FGXeFGInterpolationCount.value() < 1 || FGXeFGInterpolationCount.value() > 3))
                FGXeFGInterpolationCount.reset();
            FGXeFGIgnoreInitChecks.set_from_config(readBool("XeFG", "IgnoreInitChecks"));
            FGXeFGDepthInverted.set_from_config(readBool("XeFG", "DepthInverted"));
            FGXeFGJitteredMV.set_from_config(readBool("XeFG", "JitteredMV"));
            FGXeFGHighResMV.set_from_config(readBool("XeFG", "HighResMV"));
            FGXeFGDebugView.set_from_config(readBool("XeFG", "DebugView"));
            FGXeFGForceBorderless.set_from_config(readBool("XeFG", "ForceBorderless"));
            FGXeFGSkipResizeBuffers.set_from_config(readBool("XeFG", "SkipResizeBuffers"));
            FGXeFGModifyBufferState.set_from_config(readBool("XeFG", "ModifyBufferState"));
            FGXeFGModifySCIndex.set_from_config(readBool("XeFG", "ModifySCIndex"));
        }

        // FSR FG Inputs
        {
            FSRFGSkipConfigForHudless.set_from_config(readBool("FSRFGInputs", "SkipConfigForHudless"));
            FSRFGSkipDispatchForHudless.set_from_config(readBool("FSRFGInputs", "SkipDispatchForHudless"));
        }

        // Framerate
        {
            FramerateLimit.set_from_config(readFloat("Framerate", "FramerateLimit"));
        }

        // FSR Common
        {
            FsrVerticalFov.set_from_config(readFloat("FSR", "VerticalFov"));
            FsrHorizontalFov.set_from_config(readFloat("FSR", "HorizontalFov"));
            FsrCameraNear.set_from_config(readFloat("FSR", "CameraNear"));
            FsrCameraFar.set_from_config(readFloat("FSR", "CameraFar"));
            FsrUseFsrInputValues.set_from_config(readBool("FSR", "UseFsrInputValues"));

            FfxDx12Path.set_from_config(readWString("FSR", "FfxDx12Path"));
            FfxVkPath.set_from_config(readWString("FSR", "FfxVkPath"));
        }

        // FSR
        {
            FsrVelocity.set_from_config(readFloat("FSR", "VelocityFactor"));
            FsrReactiveScale.set_from_config(readFloat("FSR", "ReactiveScale"));
            FsrShadingScale.set_from_config(readFloat("FSR", "ShadingScale"));
            FsrAccAddPerFrame.set_from_config(readFloat("FSR", "AccAddPerFrame"));
            FsrMinDisOccAcc.set_from_config(readFloat("FSR", "MinDisOccAcc"));
            FsrDebugView.set_from_config(readBool("FSR", "DebugView"));
            FfxUpscalerIndex.set_from_config(readInt("FSR", "UpscalerIndex"));
            FfxFGIndex.set_from_config(readInt("FSR", "FGIndex"));
            FsrUseMaskForTransparency.set_from_config(readBool("FSR", "UseReactiveMaskForTransparency"));
            DlssReactiveMaskBias.set_from_config(readFloat("FSR", "DlssReactiveMaskBias"));
            Fsr4Update.set_from_config(readBool("FSR", "Fsr4Update"));
            Fsr4EnableDebugView.set_from_config(readBool("FSR", "Fsr4EnableDebugView"));
            Fsr4EnableWatermark.set_from_config(readBool("FSR", "Fsr4EnableWatermark"));

            if (auto setting = readInt("FSR", "Fsr4Model"); setting.has_value() && setting >= 0 && setting <= 5)
                Fsr4Model.set_from_config(setting);

            FsrNonLinearColorSpace.set_from_config(readBool("FSR", "FsrNonLinearColorSpace"));
            FsrNonLinearPQ.set_from_config(readBool("FSR", "FsrNonLinearPQ"));
            FsrNonLinearSRGB.set_from_config(readBool("FSR", "FsrNonLinearSRGB"));
            FsrAgilitySDKUpgrade.set_from_config(readBool("FSR", "FsrAgilitySDKUpgrade"));

            // Only sRGB or PQ should be enabled
            if (FsrNonLinearPQ.has_value() && FsrNonLinearPQ.value())
                FsrNonLinearSRGB.reset();
            else if (FsrNonLinearSRGB.has_value() && FsrNonLinearSRGB.value())
                FsrNonLinearPQ.reset();

            if (FsrNonLinearPQ.has_value() || FsrNonLinearSRGB.has_value())
                FsrNonLinearColorSpace.set_volatile_value(true);
        }

        // XeSS
        {
            BuildPipelines.set_from_config(readBool("XeSS", "BuildPipelines"));
            NetworkModel.set_from_config(readInt("XeSS", "NetworkModel"));
            CreateHeaps.set_from_config(readBool("XeSS", "CreateHeaps"));
            XeSSLibrary.set_from_config(readWString("XeSS", "LibraryPath"));
            XeSSDx11Library.set_from_config(readWString("XeSS", "Dx11LibraryPath"));
        }

        // DLSS
        {
            // Don't enable again if set false because of no nvngx found
            DLSSEnabled.set_from_config(readBool("DLSS", "Enabled"));
            NvngxPath.set_from_config(readWString("DLSS", "LibraryPath"));
            DLSSFeaturePath.set_from_config(readWString("DLSS", "FeaturePath"));
            NVNGX_DLSS_Library.set_from_config(readWString("DLSS", "NVNGX_DLSS_Path"));
            UseGenericAppIdWithDlss.set_from_config(readBool("DLSS", "UseGenericAppIdWithDlss"));

            RenderPresetOverride.set_from_config(readBool("DLSS", "RenderPresetOverride"));

            constexpr size_t presetCount = 17;

            if (auto setting = readInt("DLSS", "RenderPresetForAll");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetForAll.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetDLAA");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetDLAA.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetUltraQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetUltraQuality.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetQuality.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetBalanced");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetBalanced.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetPerformance.set_from_config(setting);

            if (auto setting = readInt("DLSS", "RenderPresetUltraPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                RenderPresetUltraPerformance.set_from_config(setting);
        }
        // DLSSD
        {
            // Don't enable again if set false because of no nvngx found
            DLSSDRenderPresetOverride.set_from_config(readBool("DLSSD", "RenderPresetOverride"));

            constexpr size_t presetCount = 6;

            if (auto setting = readInt("DLSSD", "RenderPresetForAll");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetForAll.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetDLAA");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetDLAA.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetUltraQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetUltraQuality.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetQuality");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetQuality.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetBalanced");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetBalanced.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetPerformance.set_from_config(setting);

            if (auto setting = readInt("DLSSD", "RenderPresetUltraPerformance");
                setting.has_value() && setting >= 0 && (setting < presetCount || setting == 0x00FFFFFF))
                DLSSDRenderPresetUltraPerformance.set_from_config(setting);
        }

        // Nukems
        {
            MakeDepthCopy.set_from_config(readBool("Nukems", "MakeDepthCopy"));
        }

        // Logging
        {
            LogLevel.set_from_config(readInt("Log", "LogLevel"));
            LogToConsole.set_from_config(readBool("Log", "LogToConsole"));
            LogToDebug.set_from_config(readBool("Log", "LogToDebug"));
            LogToFile.set_from_config(readBool("Log", "LogToFile"));
            LogToNGX.set_from_config(readBool("Log", "LogToNGX"));
            OpenConsole.set_from_config(readBool("Log", "OpenConsole"));
            DebugWait.set_from_config(readBool("Log", "DebugWait"));
            LogSingleFile.set_from_config(readBool("Log", "SingleFile"));
            LogAsync.set_from_config(readBool("Log", "LogAsync"));
            LogAsyncThreads.set_from_config(readInt("Log", "LogAsyncThreads"));

            {
                auto setting = readString("Log", "LogFile", false);

                if (setting.has_value() && setting.value().empty())
                    setting = std::nullopt;

                auto path = std::filesystem::path(setting.value_or(wstring_to_string(LogFileName.value_or_default())));
                auto filenameStem = path.stem();

                auto filename =
                    std::filesystem::path(LogSingleFile.value_or_default()
                                              ? filenameStem.wstring() + L".log"
                                              : filenameStem.wstring() + L"_" + std::to_wstring(GetTicks()) + L".log");

                if (setting.has_value())
                {
                    if (path.has_root_path())
                        LogFileName.set_from_config((path.parent_path() / filename).wstring());
                    else
                        LogFileName.set_from_config((Util::DllPath().parent_path() / filename).wstring());
                }
                else
                {
                    if (path.has_root_path())
                        LogFileName.set_volatile_value((path.parent_path() / filename).wstring());
                    else
                        LogFileName.set_volatile_value((Util::DllPath().parent_path() / filename).wstring());
                }
            }
        }

        // Sharpness
        {
            OverrideSharpness.set_from_config(readBool("Sharpness", "OverrideSharpness"));

            if (auto setting = readFloat("Sharpness", "Sharpness"); setting.has_value())
                Sharpness.set_from_config(std::clamp(setting.value(), 0.0f, 1.3f));
        }

        // Menu
        {
            if (auto setting = readFloat("Menu", "Scale"); setting.has_value())
                MenuScale.set_from_config(std::clamp(setting.value(), 0.5f, 2.0f));

            // Don't enable again if set false because of Linux issue
            OverlayMenu.set_from_config(readBool("Menu", "OverlayMenu"));
            ShortcutKey.set_from_config(readInt("Menu", "ShortcutKey"));
            ExtendedLimits.set_from_config(readBool("Menu", "ExtendedLimits"));
            ShowFps.set_from_config(readBool("Menu", "ShowFps"));
            UseHQFont.set_from_config(readBool("Menu", "UseHQFont"));
            DisableSplash.set_from_config(readBool("Menu", "DisableSplash"));

            if (auto setting = readInt("Menu", "FpsOverlayPos"); setting.has_value())
                FpsOverlayPos.set_from_config(std::clamp(setting.value(), 0, 3));

            if (auto setting = readUInt("Menu", "FpsOverlayType"); setting.has_value())
            {
                FpsOverlayType.set_from_config(
                    (FpsOverlay) std::clamp(setting.value(), (uint32_t) FpsOverlay_JustFPS, FpsOverlay_COUNT - 1));
            }

            FpsShortcutKey.set_from_config(readInt("Menu", "FpsShortcutKey"));
            FpsCycleShortcutKey.set_from_config(readInt("Menu", "FpsCycleShortcutKey"));
            FpsOverlayHorizontal.set_from_config(readBool("Menu", "FpsOverlayHorizontal"));

            if (auto setting = readFloat("Menu", "FpsOverlayAlpha"); setting.has_value())
                FpsOverlayAlpha.set_from_config(std::clamp(setting.value(), 0.0f, 1.0f));

            if (auto setting = readFloat("Menu", "FpsScale"); setting.has_value())
                FpsScale.set_from_config(std::clamp(setting.value(), 0.5f, 2.0f));

            TTFFontPath.set_from_config(readWString("Menu", "TTFFontPath"));

            FGShortcutKey.set_from_config(readInt("Menu", "FGShortcutKey"));
        }

        // Hooks
        {
            HookOriginalNvngxOnly.set_from_config(readBool("Hooks", "HookOriginalNvngxOnly"));
            EarlyHooking.set_from_config(readBool("Hooks", "EarlyHooking"));
            UseNtdllHooks.set_from_config(readBool("Hooks", "UseNtdllHooks"));
        }

        // RCAS
        {
            RcasEnabled.set_from_config(readBool("CAS", "Enabled"));
            MotionSharpnessEnabled.set_from_config(readBool("CAS", "MotionSharpnessEnabled"));
            MotionSharpnessDebug.set_from_config(readBool("CAS", "MotionSharpnessDebug"));

            if (auto setting = readFloat("CAS", "MotionSharpness"); setting.has_value())
                MotionSharpness.set_from_config(std::clamp(setting.value(), -1.3f, 1.3f));

            if (auto setting = readFloat("CAS", "MotionThreshold"); setting.has_value())
                MotionThreshold.set_from_config(std::clamp(setting.value(), 0.0f, 100.0f));

            if (auto setting = readFloat("CAS", "MotionScaleLimit"); setting.has_value())
                MotionScaleLimit.set_from_config(std::clamp(setting.value(), 0.01f, 100.0f));

            ContrastEnabled.set_from_config(readBool("CAS", "ContrastEnabled"));
            if (auto setting = readFloat("CAS", "Contrast"); setting.has_value())
                Contrast.set_from_config(std::clamp(setting.value(), -2.0f, 2.0f));
        }

        // Output Scaling
        {
            OutputScalingEnabled.set_from_config(readBool("OutputScaling", "Enabled"));
            OutputScalingUseFsr.set_from_config(readBool("OutputScaling", "UseFsr"));
            OutputScalingDownscaler.set_from_config(readInt("OutputScaling", "Downscaler"));

            if (auto setting = readFloat("OutputScaling", "Multiplier"); setting.has_value())
                OutputScalingMultiplier.set_from_config(std::clamp(setting.value(), 0.5f, 3.0f));
        }

        // Init Flags
        {
            AutoExposure.set_from_config(readBool("InitFlags", "AutoExposure"));
            HDR.set_from_config(readBool("InitFlags", "HDR"));
            DepthInverted.set_from_config(readBool("InitFlags", "DepthInverted"));
            JitterCancellation.set_from_config(readBool("InitFlags", "JitterCancellation"));
            DisplayResolution.set_from_config(readBool("InitFlags", "DisplayResolution"));
            DisableReactiveMask.set_from_config(readBool("InitFlags", "DisableReactiveMask"));
        }

        // DRS
        {
            DrsMinOverrideEnabled.set_from_config(readBool("DRS", "DrsMinOverrideEnabled"));
            DrsMaxOverrideEnabled.set_from_config(readBool("DRS", "DrsMaxOverrideEnabled"));
        }

        // Upscale Ratio Override
        {
            UpscaleRatioOverrideEnabled.set_from_config(readBool("UpscaleRatio", "UpscaleRatioOverrideEnabled"));
            UpscaleRatioOverrideValue.set_from_config(readFloat("UpscaleRatio", "UpscaleRatioOverrideValue"));
        }

        // Quality Overrides
        {
            QualityRatioOverrideEnabled.set_from_config(readBool("QualityOverrides", "QualityRatioOverrideEnabled"));
            QualityRatio_DLAA.set_from_config(readFloat("QualityOverrides", "QualityRatioDLAA"));
            QualityRatio_UltraQuality.set_from_config(readFloat("QualityOverrides", "QualityRatioUltraQuality"));
            QualityRatio_Quality.set_from_config(readFloat("QualityOverrides", "QualityRatioQuality"));
            QualityRatio_Balanced.set_from_config(readFloat("QualityOverrides", "QualityRatioBalanced"));
            QualityRatio_Performance.set_from_config(readFloat("QualityOverrides", "QualityRatioPerformance"));
            QualityRatio_UltraPerformance.set_from_config(
                readFloat("QualityOverrides", "QualityRatioUltraPerformance"));
        }

        // Anisotropy
        {
            if (auto setting = readInt("Anisotropy", "AnisotropyOverride");
                setting.has_value() && setting.value() <= 16 && setting.value() >= 1)
                AnisotropyOverride.set_from_config(setting);

            if (AnisotropyOverride.has_value() && (AnisotropyOverride.value() > 16 || AnisotropyOverride.value() < 1))
                AnisotropyOverride.reset();

            AnisotropySkipPointFilter.set_from_config(readBool("Anisotropy", "SkipPointFilter"));
            AnisotropyModifyComp.set_from_config(readBool("Anisotropy", "AFModifyComparison"));
            AnisotropyModifyMinMax.set_from_config(readBool("Anisotropy", "AFModifyMinMax"));
        }

        // Mipmap
        {
            if (auto setting = readFloat("Mipmap", "MipmapBiasOverride");
                setting.has_value() && setting.value() <= 15.0 && setting.value() >= -15.0)
                MipmapBiasOverride.set_from_config(setting);

            // Unsure if that's needed but it resets invalid MipmapBiasOverride on config reload
            // Unexpected place for it but could be playing a role
            if (MipmapBiasOverride.has_value() &&
                (MipmapBiasOverride.value() > 15.0 || MipmapBiasOverride.value() < -15.0))
                MipmapBiasOverride.reset();

            MipmapBiasFixedOverride.set_from_config(readBool("Mipmap", "MipmapBiasFixedOverride"));
            MipmapBiasScaleOverride.set_from_config(readBool("Mipmap", "MipmapBiasScaleOverride"));
            MipmapBiasOverrideAll.set_from_config(readBool("Mipmap", "MipmapBiasOverrideAll"));
        }

        // Process Filter
        {
            ProcessExclusionList.set_from_config(readWString("ProcessFilter", "ProcessExclusionList", true));
            TargetProcess.set_from_config(readWString("ProcessFilter", "TargetProcessName", true));
        }

        // Hotfixes
        {
            CheckForUpdate.set_from_config(readBool("Hotfix", "CheckForUpdate"));
            DisableOverlays.set_from_config(readBool("Hotfix", "DisableOverlays"));

            RoundInternalResolution.set_from_config(readInt("Hotfix", "RoundInternalResolution"));

            RestoreComputeSignature.set_from_config(readBool("Hotfix", "RestoreComputeSignature"));
            RestoreGraphicSignature.set_from_config(readBool("Hotfix", "RestoreGraphicSignature"));
            PreferDedicatedGpu.set_from_config(readBool("Hotfix", "PreferDedicatedGpu"));
            PreferFirstDedicatedGpu.set_from_config(readBool("Hotfix", "PreferFirstDedicatedGpu"));
            SkipFirstFrames.set_from_config(readInt("Hotfix", "SkipFirstFrames"));
            UsePrecompiledShaders.set_from_config(readBool("Hotfix", "UsePrecompiledShaders"));
            ColorResourceBarrier.set_from_config(readInt("Hotfix", "ColorResourceBarrier"));
            MVResourceBarrier.set_from_config(readInt("Hotfix", "MotionVectorResourceBarrier"));
            DepthResourceBarrier.set_from_config(readInt("Hotfix", "DepthResourceBarrier"));
            MaskResourceBarrier.set_from_config(readInt("Hotfix", "ColorMaskResourceBarrier"));
            ExposureResourceBarrier.set_from_config(readInt("Hotfix", "ExposureResourceBarrier"));
            OutputResourceBarrier.set_from_config(readInt("Hotfix", "OutputResourceBarrier"));
            DontCreateD3D12DeviceForLuma.set_from_config(readBool("Hotfix", "DontCreateD3D12DeviceForLuma"));
        }

        // Dx11 with Dx12
        {
            Dx11DelayedInit.set_from_config(readInt("Dx11withDx12", "UseDelayedInit"));
            DontUseNTShared.set_from_config(readBool("Dx11withDx12", "DontUseNTShared"));
        }

        // NvApi
        {
            OverrideNvapiDll.set_from_config(readBool("NvApi", "OverrideNvapiDll"));
            NvapiDllPath.set_from_config(readWString("NvApi", "NvapiDllPath", true));
            DisableFlipMetering.set_from_config(readBool("NvApi", "DisableFlipMetering"));
        }

        // Spoofing
        {
            DxgiSpoofing.set_from_config(readBool("Spoofing", "Dxgi"));
            DxgiFactoryWrapping.set_from_config(readBool("Spoofing", "DxgiFactoryWrapping"));
            DxgiBlacklist.set_from_config(readString("Spoofing", "DxgiBlacklist"));
            DxgiVRAM.set_from_config(readInt("Spoofing", "DxgiVRAM"));
            VulkanSpoofing.set_from_config(readBool("Spoofing", "Vulkan"));
            VulkanExtensionSpoofing.set_from_config(readBool("Spoofing", "VulkanExtensionSpoofing"));
            VulkanVRAM.set_from_config(readInt("Spoofing", "VulkanVRAM"));
            SpoofedGPUName.set_from_config(readWString("Spoofing", "SpoofedGPUName"));
            StreamlineSpoofing.set_from_config(readBool("Spoofing", "StreamlineSpoofing"));
            SpoofHAGS.set_from_config(readBool("Spoofing", "SpoofHAGS"));
            SpoofFeatureLevel.set_from_config(readBool("Spoofing", "D3DFeatureLevel"));
            SpoofedVendorId.set_from_config(readUInt("Spoofing", "SpoofedVendorId"));
            SpoofedDeviceId.set_from_config(readUInt("Spoofing", "SpoofedDeviceId"));
            TargetVendorId.set_from_config(readUInt("Spoofing", "TargetVendorId"));
            TargetDeviceId.set_from_config(readUInt("Spoofing", "TargetDeviceId"));
            UESpoofIntelAtomics64.set_from_config(readBool("Spoofing", "UEIntelAtomics"));
            SpoofRegistry.set_from_config(readBool("Spoofing", "Registry"));
            SpoofedDriver.set_from_config(readWString("Spoofing", "RegistryDriver"));
        }

        // Inputs
        {
            EnableDlssInputs.set_from_config(readBool("Inputs", "EnableDlssInputs"));
            EnableXeSSInputs.set_from_config(readBool("Inputs", "EnableXeSSInputs"));

            EnableFsr2Inputs.set_from_config(readBool("Inputs", "EnableFsr2Inputs"));
            UseFsr2Inputs.set_from_config(readBool("Inputs", "UseFsr2Inputs"));
            UseFsr2Dx11Inputs.set_from_config(readBool("Inputs", "UseFsr2Dx11Inputs"));
            UseFsr2VulkanInputs.set_from_config(readBool("Inputs", "UseFsr2VulkanInputs"));
            Fsr2Pattern.set_from_config(readBool("Inputs", "Fsr2Pattern"));

            EnableFsr3Inputs.set_from_config(readBool("Inputs", "EnableFsr3Inputs"));
            UseFsr3Inputs.set_from_config(readBool("Inputs", "UseFsr3Inputs"));
            Fsr3Pattern.set_from_config(readBool("Inputs", "Fsr3Pattern"));

            EnableFfxInputs.set_from_config(readBool("Inputs", "EnableFfxInputs"));
            UseFfxInputs.set_from_config(readBool("Inputs", "UseFfxInputs"));
            EnableHotSwapping.set_from_config(readBool("Inputs", "EnableHotSwapping"));
        }

        // Plugins
        {
            std::filesystem::path path;
            auto setting = readString("Plugins", "Path", true);

            if (setting.has_value())
                path = std::filesystem::path(setting.value());
            else
                path = std::filesystem::path(PluginPath.value_or_default());

            if (setting.has_value())
            {
                if (path.has_root_path())
                    PluginPath.set_from_config(path.wstring());
                else
                    PluginPath.set_from_config((Util::DllPath().parent_path() / path).wstring());
            }
            else
            {
                if (path.has_root_path())
                    PluginPath.set_volatile_value(path.wstring());
                else
                    PluginPath.set_volatile_value((Util::DllPath().parent_path() / path).wstring());
            }

            LoadSpecialK.set_from_config(readBool("Plugins", "LoadSpecialK"));
            LoadReShade.set_from_config(readBool("Plugins", "LoadReShade"));
            LoadAsiPlugins.set_from_config(readBool("Plugins", "LoadAsiPlugins"));
        }

        // HDR
        {
            ForceHDR.set_from_config(readBool("HDR", "ForceHDR"));
            UseHDR10.set_from_config(readBool("HDR", "UseHDR10"));
            SkipColorSpace.set_from_config(readBool("HDR", "SkipColorSpace"));
        }

        // V-Sync
        {
            OverrideVsync.set_from_config(readBool("V-Sync", "OverrideVsync"));
            ForceVsync.set_from_config(readBool("V-Sync", "ForceVsync"));
            VsyncInterval.set_from_config(readInt("V-Sync", "SyncInterval"));
        }

        if (fakenvapi::isUsingFakenvapi())
            return ReloadFakenvapi();

        return true;
    }

    return false;
}

bool Config::LoadFromPath(const wchar_t* InPath)
{
    std::filesystem::path iniPath(InPath);
    auto newPath = iniPath / fileName;

    if (Reload(newPath))
    {
        absoluteFileName = newPath;
        return true;
    }

    return false;
}

std::string GetBoolValue(std::optional<bool> value)
{
    if (!value.has_value())
        return "auto";

    return value.value() ? "true" : "false";
}

std::string GetIntValue(std::optional<int> value, bool getHex = false)
{
    if (!value.has_value())
        return "auto";

    if (getHex)
        return std::format("{:#x}", value.value());

    return std::to_string(value.value());
}

std::string GetFloatValue(std::optional<float> value)
{
    if (!value.has_value())
        return "auto";

    return std::to_string(value.value());
}

// ========== SaveIni Helper Macros ==========
// These macros reduce boilerplate in SaveIni() while handling all CustomOptional variants.
// They work because value_for_config() returns std::optional<T> regardless of
// whether CustomOptional has WithDefault, NoDefault, or SoftDefault.

#define SAVE_BOOL(sec, key, opt) \
    ini.SetValue(sec, key, GetBoolValue(Instance()->opt.value_for_config()).c_str())

#define SAVE_INT(sec, key, opt) \
    ini.SetValue(sec, key, GetIntValue(Instance()->opt.value_for_config()).c_str())

#define SAVE_INT_HEX(sec, key, opt) \
    ini.SetValue(sec, key, GetIntValue(Instance()->opt.value_for_config(), true).c_str())

#define SAVE_INT_FORCE(sec, key, opt, force) \
    ini.SetValue(sec, key, GetIntValue(Instance()->opt.value_for_config(), force).c_str())

#define SAVE_FLOAT(sec, key, opt) \
    ini.SetValue(sec, key, GetFloatValue(Instance()->opt.value_for_config()).c_str())

#define SAVE_STRING(sec, key, opt, def) \
    ini.SetValue(sec, key, Instance()->opt.value_for_config_or(def).c_str())

#define SAVE_WSTRING(sec, key, opt, def) \
    ini.SetValue(sec, key, wstring_to_string(Instance()->opt.value_for_config_or(def)).c_str())

#define SAVE_FLOAT_FORCE(sec, key, opt, force) \
    ini.SetValue(sec, key, GetFloatValue(Instance()->opt.value_for_config(force)).c_str())

bool Config::SaveIni()
{
    std::lock_guard<std::mutex> lock(iniMutex);
    // Upscalers
    {
        SAVE_STRING("Upscalers", "Dx11Upscaler", Dx11Upscaler, "auto");
        SAVE_STRING("Upscalers", "Dx12Upscaler", Dx12Upscaler, "auto");
        SAVE_STRING("Upscalers", "VulkanUpscaler", VulkanUpscaler, "auto");
    }

    // Frame Generation
    {
        SAVE_BOOL("FrameGen", "Enabled", FGEnabled);
        SAVE_BOOL("FrameGen", "DebugView", FGDebugView);
        std::string FGInputString = "auto";
        if (auto FGInputHeld = Instance()->FGInput.value_for_config(); FGInputHeld.has_value())
        {
            if (FGInputHeld.value() == FGInput::NoFG)
                FGInputString = "NoFG";
            else if (FGInputHeld.value() == FGInput::Upscaler)
                FGInputString = "Upscaler";
            else if (FGInputHeld.value() == FGInput::Nukems)
                FGInputString = "Nukems";
            else if (FGInputHeld.value() == FGInput::DLSSG)
                FGInputString = "DLSSG";
            else if (FGInputHeld.value() == FGInput::FSRFG)
                FGInputString = "FSRFG";
            else if (FGInputHeld.value() == FGInput::FSRFG30)
                FGInputString = "FSRFG30";
        }
        ini.SetValue("FrameGen", "FGInput", FGInputString.c_str());

        std::string FGOutputString = "auto";
        if (auto FGOutputHeld = Instance()->FGOutput.value_for_config(); FGOutputHeld.has_value())
        {
            if (FGOutputHeld.value() == FGOutput::NoFG)
                FGOutputString = "NoFG";
            else if (FGOutputHeld.value() == FGOutput::FSRFG)
                FGOutputString = "FSRFG";
            else if (FGOutputHeld.value() == FGOutput::Nukems)
                FGOutputString = "Nukems";
            else if (FGOutputHeld.value() == FGOutput::XeFG)
                FGOutputString = "XeFG";
        }
        ini.SetValue("FrameGen", "FGOutput", FGOutputString.c_str());
        SAVE_BOOL("FrameGen", "DrawUIOverFG", FGDrawUIOverFG);
        SAVE_BOOL("FrameGen", "UIPremultipliedAlpha", FGUIPremultipliedAlpha);
        SAVE_BOOL("FrameGen", "DisableHudless", FGDisableHudless);
        SAVE_BOOL("FrameGen", "DisableUI", FGDisableUI);
        SAVE_BOOL("FrameGen", "SkipReset", FGSkipReset);
        SAVE_INT("FrameGen", "RectLeft", FGRectLeft);
        SAVE_INT("FrameGen", "RectTop", FGRectTop);
        SAVE_INT("FrameGen", "RectWidth", FGRectWidth);
        SAVE_INT("FrameGen", "RectHeight", FGRectHeight);
        SAVE_INT("FrameGen", "AllowedFrameAhead", FGAllowedFrameAhead);
        SAVE_BOOL("FrameGen", "DepthValidNow", FGDepthValidNow);
        SAVE_BOOL("FrameGen", "VelocityValidNow", FGVelocityValidNow);
        SAVE_BOOL("FrameGen", "HudlessValidNow", FGHudlessValidNow);
        SAVE_BOOL("FrameGen", "OnlyAcceptFirstHudless", FGOnlyAcceptFirstHudless);
    }

    // FSR FG output
    {
        SAVE_BOOL("FSRFG", "DebugTearLines", FGDebugTearLines);
        SAVE_BOOL("FSRFG", "DebugResetLines", FGDebugResetLines);
        SAVE_BOOL("FSRFG", "DebugPacingLines", FGDebugPacingLines);
        SAVE_BOOL("FSRFG", "AllowAsync", FGAsync);
        SAVE_BOOL("FSRFG", "UseMutexForSwapchain", FGUseMutexForSwapchain);
        SAVE_BOOL("FSRFG", "FramePacingTuning", FGFramePacingTuning);
        SAVE_FLOAT("FSRFG", "FPTSafetyMarginInMs", FGFPTSafetyMarginInMs);
        SAVE_FLOAT("FSRFG", "FPTVarianceFactor", FGFPTVarianceFactor);
        SAVE_BOOL("FSRFG", "FPTHybridSpin", FGFPTAllowHybridSpin);
        SAVE_INT("FSRFG", "FPTHybridSpinTime", FGFPTHybridSpinTime);
        SAVE_BOOL("FSRFG", "FPTWaitForSingleObjectOnFence", FGFPTAllowWaitForSingleObjectOnFence);
        SAVE_BOOL("FSRFG", "EnableWatermark", FSRFGEnableWatermark);
    }

    // XeFG output
    {
        SAVE_INT("XeFG", "InterpolationCount", FGXeFGInterpolationCount);
        SAVE_BOOL("XeFG", "IgnoreInitChecks", FGXeFGIgnoreInitChecks);
        SAVE_BOOL("XeFG", "DepthInverted", FGXeFGDepthInverted);
        SAVE_BOOL("XeFG", "JitteredMV", FGXeFGJitteredMV);
        SAVE_BOOL("XeFG", "HighResMV", FGXeFGHighResMV);
        SAVE_BOOL("XeFG", "DebugView", FGXeFGDebugView);
        SAVE_BOOL("XeFG", "ForceBorderless", FGXeFGForceBorderless);
        SAVE_BOOL("XeFG", "SkipResizeBuffers", FGXeFGSkipResizeBuffers);
        SAVE_BOOL("XeFG", "ModifyBufferState", FGXeFGModifyBufferState);
        SAVE_BOOL("XeFG", "ModifySCIndex", FGXeFGModifySCIndex);
    }

    // OptiFG
    {
        SAVE_BOOL("OptiFG", "HUDFix", FGHUDFix);
        SAVE_INT("OptiFG", "HUDLimit", FGHUDLimit);
        SAVE_BOOL("OptiFG", "HUDFixExtended", FGHUDFixExtended);
        SAVE_BOOL("OptiFG", "HUDFixImmediate", FGImmediateCapture);
        SAVE_BOOL("OptiFG", "UseShards", FGUseShards);
        SAVE_BOOL("OptiFG", "AlwaysTrackHeaps", FGAlwaysTrackHeaps);
        SAVE_BOOL("OptiFG", "ResourceBlocking", FGResourceBlocking);
        SAVE_BOOL("OptiFG", "MakeDepthCopy", FGMakeDepthCopy);
        SAVE_BOOL("OptiFG", "MakeMVCopy", FGMakeMVCopy);

        SAVE_BOOL("OptiFG", "HudfixDisableRTV", FGHudfixDisableRTV);
        SAVE_BOOL("OptiFG", "HudfixDisableSRV", FGHudfixDisableSRV);
        SAVE_BOOL("OptiFG", "HudfixDisableUAV", FGHudfixDisableUAV);
        SAVE_BOOL("OptiFG", "HudfixDisableOM", FGHudfixDisableOM);
        SAVE_BOOL("OptiFG", "HudfixDisableDispatch", FGHudfixDisableDispatch);
        SAVE_BOOL("OptiFG", "HudfixDisableDI", FGHudfixDisableDI);
        SAVE_BOOL("OptiFG", "HudfixDisableDII", FGHudfixDisableDII);
        SAVE_BOOL("OptiFG", "HudfixDisableSCR", FGHudfixDisableSCR);
        SAVE_BOOL("OptiFG", "HudfixDisableSGR", FGHudfixDisableSGR);

        SAVE_BOOL("OptiFG", "EnableDepthScale", FGEnableDepthScale);
        SAVE_FLOAT("OptiFG", "DepthScaleMax", FGDepthScaleMax);

        SAVE_BOOL("OptiFG", "HUDFixDontUseSwapchainBuffers", FGDontUseSwapchainBuffers);
        SAVE_BOOL("OptiFG", "HUDFixRelaxedResolutionCheck", FGRelaxedResolutionCheck);
        SAVE_BOOL("OptiFG", "ResourceFlip", FGResourceFlip);
        SAVE_BOOL("OptiFG", "ResourceFlipOffset", FGResourceFlipOffset);

        SAVE_BOOL("OptiFG", "AlwaysCaptureFSRFGSwapchain", FGAlwaysCaptureFSRFGSwapchain);
    }

    // FSR FG Inputs
    {
        SAVE_BOOL("FSRFGInputs", "SkipConfigForHudless", FSRFGSkipConfigForHudless);
        SAVE_BOOL("FSRFGInputs", "SkipDispatchForHudless", FSRFGSkipDispatchForHudless);
    }

    // Framerate
    {
        SAVE_FLOAT("Framerate", "FramerateLimit", FramerateLimit);
    }

    // Output Scaling
    {
        SAVE_BOOL("OutputScaling", "Enabled", OutputScalingEnabled);
        SAVE_FLOAT("OutputScaling", "Multiplier", OutputScalingMultiplier);
        SAVE_BOOL("OutputScaling", "UseFsr", OutputScalingUseFsr);
        SAVE_INT("OutputScaling", "Downscaler", OutputScalingDownscaler);
    }

    // FSR common
    {
        SAVE_FLOAT("FSR", "VerticalFov", FsrVerticalFov);
        SAVE_FLOAT("FSR", "HorizontalFov", FsrHorizontalFov);
        SAVE_FLOAT("FSR", "CameraNear", FsrCameraNear);
        SAVE_FLOAT("FSR", "CameraFar", FsrCameraFar);
        SAVE_BOOL("FSR", "UseFsrInputValues", FsrUseFsrInputValues);

        SAVE_WSTRING("FSR", "FfxDx12Path", FfxDx12Path, L"auto");
        SAVE_WSTRING("FSR", "FfxVkPath", FfxVkPath, L"auto");
    }

    // FSR
    {
        SAVE_FLOAT("FSR", "VelocityFactor", FsrVelocity);
        SAVE_FLOAT("FSR", "ReactiveScale", FsrReactiveScale);
        SAVE_FLOAT("FSR", "ShadingScale", FsrShadingScale);
        SAVE_FLOAT("FSR", "AccAddPerFrame", FsrAccAddPerFrame);
        SAVE_FLOAT("FSR", "MinDisOccAcc", FsrMinDisOccAcc);
        SAVE_BOOL("FSR", "DebugView", FsrDebugView);
        SAVE_INT("FSR", "UpscalerIndex", FfxUpscalerIndex);
        SAVE_INT("FSR", "FGIndex", FfxFGIndex);
        SAVE_BOOL("FSR", "UseReactiveMaskForTransparency", FsrUseMaskForTransparency);
        SAVE_FLOAT("FSR", "DlssReactiveMaskBias", DlssReactiveMaskBias);
        ini.SetValue("FSR", "Fsr4Update",
                     GetBoolValue(Instance()->Fsr4Update.value_for_config_ignore_default()).c_str());
        SAVE_INT("FSR", "Fsr4Model", Fsr4Model);
        SAVE_BOOL("FSR", "Fsr4EnableDebugView", Fsr4EnableDebugView);
        SAVE_BOOL("FSR", "Fsr4EnableWatermark", Fsr4EnableWatermark);
        SAVE_BOOL("FSR", "FsrNonLinearColorSpace", FsrNonLinearColorSpace);
        SAVE_BOOL("FSR", "FsrNonLinearPQ", FsrNonLinearPQ);
        SAVE_BOOL("FSR", "FsrNonLinearSRGB", FsrNonLinearSRGB);
        SAVE_BOOL("FSR", "FsrAgilitySDKUpgrade", FsrAgilitySDKUpgrade);
    }

    // XeSS
    {
        SAVE_BOOL("XeSS", "BuildPipelines", BuildPipelines);
        SAVE_BOOL("XeSS", "CreateHeaps", CreateHeaps);
        SAVE_INT("XeSS", "NetworkModel", NetworkModel);
        SAVE_WSTRING("XeSS", "LibraryPath", XeSSLibrary, L"auto");
        SAVE_WSTRING("XeSS", "Dx11LibraryPath", XeSSDx11Library, L"auto");
    }

    // DLSS
    {
        SAVE_BOOL("DLSS", "Enabled", DLSSEnabled);
        SAVE_WSTRING("DLSS", "LibraryPath", NvngxPath, L"auto");
        SAVE_WSTRING("DLSS", "FeaturePath", DLSSFeaturePath, L"auto");
        SAVE_WSTRING("DLSS", "NVNGX_DLSS_Path", NVNGX_DLSS_Library, L"auto");
        SAVE_BOOL("DLSS", "RenderPresetOverride", RenderPresetOverride);
        SAVE_INT("DLSS", "RenderPresetForAll", RenderPresetForAll);
        SAVE_INT("DLSS", "RenderPresetDLAA", RenderPresetDLAA);
        SAVE_INT("DLSS", "RenderPresetUltraQuality", RenderPresetUltraQuality);
        SAVE_INT("DLSS", "RenderPresetQuality", RenderPresetQuality);
        SAVE_INT("DLSS", "RenderPresetBalanced", RenderPresetBalanced);
        SAVE_INT("DLSS", "RenderPresetPerformance", RenderPresetPerformance);
        SAVE_INT("DLSS", "RenderPresetUltraPerformance", RenderPresetUltraPerformance);
        SAVE_BOOL("DLSS", "UseGenericAppIdWithDlss", UseGenericAppIdWithDlss);
    }

    // DLSSD
    {
        SAVE_BOOL("DLSSD", "RenderPresetOverride", DLSSDRenderPresetOverride);
        SAVE_INT("DLSSD", "RenderPresetForAll", DLSSDRenderPresetForAll);
        SAVE_INT("DLSSD", "RenderPresetDLAA", DLSSDRenderPresetDLAA);
        SAVE_INT("DLSSD", "RenderPresetUltraQuality", DLSSDRenderPresetUltraQuality);
        SAVE_INT("DLSSD", "RenderPresetQuality", DLSSDRenderPresetQuality);
        SAVE_INT("DLSSD", "RenderPresetBalanced", DLSSDRenderPresetBalanced);
        SAVE_INT("DLSSD", "RenderPresetPerformance", DLSSDRenderPresetPerformance);
        SAVE_INT("DLSSD", "RenderPresetUltraPerformance", DLSSDRenderPresetUltraPerformance);
    }

    // Nukems
    {
        SAVE_BOOL("Nukems", "MakeDepthCopy", MakeDepthCopy);
    }

    // Sharpness
    {
        SAVE_BOOL("Sharpness", "OverrideSharpness", OverrideSharpness);
        SAVE_FLOAT("Sharpness", "Sharpness", Sharpness);
    }

    // Menu
    {
        ini.SetValue("Menu", "Scale", GetFloatValue(Instance()->MenuScale.value_for_config(true)).c_str());
        SAVE_BOOL("Menu", "OverlayMenu", OverlayMenu);

        auto setting = Instance()->ShortcutKey.value_for_config();
        ini.SetValue("Menu", "ShortcutKey",
                     GetIntValue(Instance()->ShortcutKey.value_for_config(), setting > 0).c_str());

        SAVE_BOOL("Menu", "ExtendedLimits", ExtendedLimits);
        SAVE_BOOL("Menu", "ShowFps", ShowFps);
        SAVE_BOOL("Menu", "UseHQFont", UseHQFont);
        SAVE_BOOL("Menu", "DisableSplash", DisableSplash);

        setting = Instance()->FGShortcutKey.value_for_config();
        ini.SetValue("Menu", "FGShortcutKey",
                     GetIntValue(Instance()->FGShortcutKey.value_for_config(), setting > 0).c_str());

        setting = Instance()->FpsShortcutKey.value_for_config();
        ini.SetValue("Menu", "FpsShortcutKey",
                     GetIntValue(Instance()->FpsShortcutKey.value_for_config(), setting > 0).c_str());

        setting = Instance()->FpsCycleShortcutKey.value_for_config();
        ini.SetValue("Menu", "FpsCycleShortcutKey",
                     GetIntValue(Instance()->FpsCycleShortcutKey.value_for_config(), setting > 0).c_str());

        SAVE_INT("Menu", "FpsOverlayPos", FpsOverlayPos);
        SAVE_INT("Menu", "FpsOverlayType", FpsOverlayType);
        SAVE_BOOL("Menu", "FpsOverlayHorizontal", FpsOverlayHorizontal);
        SAVE_FLOAT("Menu", "FpsOverlayAlpha", FpsOverlayAlpha);
        SAVE_FLOAT("Menu", "FpsScale", FpsScale);
        SAVE_WSTRING("Menu", "TTFFontPath", TTFFontPath, L"auto");
    }

    // Hooks
    {
        SAVE_BOOL("Hooks", "HookOriginalNvngxOnly", HookOriginalNvngxOnly);
        SAVE_BOOL("Hooks", "EarlyHooking", EarlyHooking);
        SAVE_BOOL("Hooks", "UseNtdllHooks", UseNtdllHooks);
    }

    // CAS
    {
        ini.SetValue("CAS", "Enabled",
                     Instance()->RcasEnabled.has_value() ? (Instance()->RcasEnabled.value() ? "true" : "false")
                                                         : "auto");
        SAVE_BOOL("CAS", "MotionSharpnessEnabled", MotionSharpnessEnabled);
        SAVE_BOOL("CAS", "MotionSharpnessDebug", MotionSharpnessDebug);
        SAVE_FLOAT("CAS", "MotionSharpness", MotionSharpness);
        SAVE_FLOAT("CAS", "MotionThreshold", MotionThreshold);
        SAVE_FLOAT("CAS", "MotionScaleLimit", MotionScaleLimit);
        SAVE_BOOL("CAS", "ContrastEnabled", ContrastEnabled);
        SAVE_FLOAT("CAS", "Contrast", Contrast);
    }

    // InitFlags
    {
        SAVE_BOOL("InitFlags", "AutoExposure", AutoExposure);
        SAVE_BOOL("InitFlags", "HDR", HDR);
        SAVE_BOOL("InitFlags", "DepthInverted", DepthInverted);
        SAVE_BOOL("InitFlags", "JitterCancellation", JitterCancellation);
        SAVE_BOOL("InitFlags", "DisplayResolution", DisplayResolution);
        SAVE_BOOL("InitFlags", "DisableReactiveMask", DisableReactiveMask);
    }

    // Upscale Ratio Override
    {
        SAVE_BOOL("UpscaleRatio", "UpscaleRatioOverrideEnabled", UpscaleRatioOverrideEnabled);
        SAVE_FLOAT("UpscaleRatio", "UpscaleRatioOverrideValue", UpscaleRatioOverrideValue);
    }

    // Quality Overrides
    {
        SAVE_BOOL("QualityOverrides", "QualityRatioOverrideEnabled", QualityRatioOverrideEnabled);
        SAVE_FLOAT("QualityOverrides", "QualityRatioDLAA", QualityRatio_DLAA);
        SAVE_FLOAT("QualityOverrides", "QualityRatioUltraQuality", QualityRatio_UltraQuality);
        SAVE_FLOAT("QualityOverrides", "QualityRatioQuality", QualityRatio_Quality);
        SAVE_FLOAT("QualityOverrides", "QualityRatioBalanced", QualityRatio_Balanced);
        SAVE_FLOAT("QualityOverrides", "QualityRatioPerformance", QualityRatio_Performance);
        SAVE_FLOAT("QualityOverrides", "QualityRatioUltraPerformance", QualityRatio_UltraPerformance);
    }

    // Anisotropy
    {
        SAVE_INT("Anisotropy", "AnisotropyOverride", AnisotropyOverride);
        SAVE_BOOL("Anisotropy", "ModifyComparison", AnisotropyModifyComp);
        SAVE_BOOL("Anisotropy", "ModifyMinMax", AnisotropyModifyMinMax);
        SAVE_BOOL("Anisotropy", "SkipPointFilter", AnisotropySkipPointFilter);
    }

    // Mipmap
    {
        SAVE_FLOAT("Mipmap", "MipmapBiasOverride", MipmapBiasOverride);
        SAVE_BOOL("Mipmap", "MipmapBiasOverrideAll", MipmapBiasOverrideAll);
        SAVE_BOOL("Mipmap", "MipmapBiasFixedOverride", MipmapBiasFixedOverride);
        SAVE_BOOL("Mipmap", "MipmapBiasScaleOverride", MipmapBiasScaleOverride);
    }

    // Process Filter
    {
        SAVE_WSTRING("ProcessFilter", "TargetProcessName", TargetProcess, L"auto");
        SAVE_WSTRING("ProcessFilter", "ProcessExclusionList", ProcessExclusionList, L"auto");
    }

    // Hotfixes
    {
        SAVE_BOOL("Hotfix", "DontCreateD3D12DeviceForLuma", DontCreateD3D12DeviceForLuma);
        SAVE_BOOL("Hotfix", "CheckForUpdate", CheckForUpdate);
        SAVE_BOOL("Hotfix", "DisableOverlays", DisableOverlays);

        SAVE_INT("Hotfix", "RoundInternalResolution", RoundInternalResolution);

        SAVE_BOOL("Hotfix", "RestoreComputeSignature", RestoreComputeSignature);
        SAVE_BOOL("Hotfix", "RestoreGraphicSignature", RestoreGraphicSignature);
        SAVE_INT("Hotfix", "SkipFirstFrames", SkipFirstFrames);

        SAVE_BOOL("Hotfix", "UsePrecompiledShaders", UsePrecompiledShaders);
        SAVE_BOOL("Hotfix", "PreferDedicatedGpu", PreferDedicatedGpu);
        SAVE_BOOL("Hotfix", "PreferFirstDedicatedGpu", PreferFirstDedicatedGpu);

        SAVE_INT("Hotfix", "ColorResourceBarrier", ColorResourceBarrier);
        SAVE_INT("Hotfix", "MotionVectorResourceBarrier", MVResourceBarrier);
        SAVE_INT("Hotfix", "DepthResourceBarrier", DepthResourceBarrier);
        SAVE_INT("Hotfix", "ColorMaskResourceBarrier", MaskResourceBarrier);
        SAVE_INT("Hotfix", "ExposureResourceBarrier", ExposureResourceBarrier);
        SAVE_INT("Hotfix", "OutputResourceBarrier", OutputResourceBarrier);
    }

    // Dx11 with Dx12
    {
        SAVE_BOOL("Dx11withDx12", "DontUseNTShared", DontUseNTShared);
    }

    // Logging
    {
        SAVE_INT("Log", "LogLevel", LogLevel);
        SAVE_BOOL("Log", "LogToConsole", LogToConsole);
        SAVE_BOOL("Log", "LogToDebug", LogToDebug);
        SAVE_BOOL("Log", "LogToFile", LogToFile);
        SAVE_BOOL("Log", "LogToNGX", LogToNGX);
        SAVE_BOOL("Log", "OpenConsole", OpenConsole);
        SAVE_WSTRING("Log", "LogFile", LogFileName, L"auto");
        SAVE_BOOL("Log", "SingleFile", LogSingleFile);
        SAVE_BOOL("Log", "LogAsync", LogAsync);
        SAVE_INT("Log", "LogAsyncThreads", LogAsyncThreads);
    }

    // NvApi
    {
        SAVE_BOOL("NvApi", "OverrideNvapiDll", OverrideNvapiDll);
        SAVE_WSTRING("NvApi", "NvapiDllPath", NvapiDllPath, L"auto");
        SAVE_BOOL("NvApi", "DisableFlipMetering", DisableFlipMetering);
    }

    // DRS
    {
        SAVE_BOOL("DRS", "DrsMinOverrideEnabled", DrsMinOverrideEnabled);
        SAVE_BOOL("DRS", "DrsMaxOverrideEnabled", DrsMaxOverrideEnabled);
    }

    // Spoofing
    {
        // Save Dxgi spoofing value only if it differs from the current GPU vendor
        bool forceSaveDxgi = Instance()->DxgiSpoofing.has_value() &&
                             ((State::Instance().isRunningOnNvidia && Instance()->DxgiSpoofing.value()) ||
                              (!State::Instance().isRunningOnNvidia && !Instance()->DxgiSpoofing.value()));

        ini.SetValue("Spoofing", "Dxgi",
                     GetBoolValue(Instance()->DxgiSpoofing.value_for_config(forceSaveDxgi)).c_str());
        SAVE_BOOL("Spoofing", "DxgiFactoryWrapping", DxgiFactoryWrapping);
        SAVE_STRING("Spoofing", "DxgiBlacklist", DxgiBlacklist, "auto");
        SAVE_BOOL("Spoofing", "Vulkan", VulkanSpoofing);
        SAVE_BOOL("Spoofing", "VulkanExtensionSpoofing", VulkanExtensionSpoofing);
        SAVE_INT("Spoofing", "VulkanVRAM", VulkanVRAM);
        SAVE_INT("Spoofing", "DxgiVRAM", DxgiVRAM);
        SAVE_WSTRING("Spoofing", "SpoofedGPUName", SpoofedGPUName, L"auto");
        SAVE_BOOL("Spoofing", "StreamlineSpoofing", StreamlineSpoofing);
        SAVE_BOOL("Spoofing", "SpoofHAGS", SpoofHAGS);
        SAVE_BOOL("Spoofing", "D3DFeatureLevel", SpoofFeatureLevel);
        SAVE_BOOL("Spoofing", "UEIntelAtomics", UESpoofIntelAtomics64);
        SAVE_INT_HEX("Spoofing", "SpoofedVendorId", SpoofedVendorId);
        SAVE_INT_HEX("Spoofing", "SpoofedDeviceId", SpoofedDeviceId);
        SAVE_INT_HEX("Spoofing", "TargetVendorId", TargetVendorId);
        SAVE_INT_HEX("Spoofing", "TargetDeviceId", TargetDeviceId);
        SAVE_BOOL("Spoofing", "Registry", SpoofRegistry);
        SAVE_WSTRING("Spoofing", "RegistryDriver", SpoofedDriver, L"auto");

        // Enable HAGS when DLSS-G will be used
        if (!Instance()->SpoofHAGS.has_value())
        {
            Instance()->SpoofHAGS.set_volatile_value(Instance()->FGInput.value_or_default() == FGInput::Nukems ||
                                                     Instance()->FGInput.value_or_default() == FGInput::DLSSG);
        }
    }

    // Plugins
    {

        ini.SetValue("Plugins", "Path", wstring_to_string(Instance()->PluginPath.value_for_config_or(L"auto")).c_str());
        ini.SetValue("Plugins", "LoadSpecialK", GetBoolValue(Instance()->LoadSpecialK.value_for_config()).c_str());
        ini.SetValue("Plugins", "LoadReShade", GetBoolValue(Instance()->LoadReShade.value_for_config()).c_str());
        ini.SetValue("Plugins", "LoadAsiPlugins", GetBoolValue(Instance()->LoadAsiPlugins.value_for_config()).c_str());
    }

    // inputs
    {
        ini.SetValue("Inputs", "EnableDlssInputs",
                     GetBoolValue(Instance()->EnableDlssInputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "EnableXeSSInputs",
                     GetBoolValue(Instance()->EnableXeSSInputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "UseFsr2Inputs", GetBoolValue(Instance()->UseFsr2Inputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "UseFsr2Dx11Inputs",
                     GetBoolValue(Instance()->UseFsr2Dx11Inputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "UseFsr2VulkanInputs",
                     GetBoolValue(Instance()->UseFsr2VulkanInputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "Fsr2Pattern", GetBoolValue(Instance()->Fsr2Pattern.value_for_config()).c_str());
        ini.SetValue("Inputs", "UseFsr3Inputs", GetBoolValue(Instance()->UseFsr3Inputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "Fsr3Pattern", GetBoolValue(Instance()->Fsr3Pattern.value_for_config()).c_str());
        ini.SetValue("Inputs", "UseFfxInputs", GetBoolValue(Instance()->UseFfxInputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "EnableHotSwapping",
                     GetBoolValue(Instance()->EnableHotSwapping.value_for_config()).c_str());

        ini.SetValue("Inputs", "EnableFsr2Inputs",
                     GetBoolValue(Instance()->EnableFsr2Inputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "EnableFsr3Inputs",
                     GetBoolValue(Instance()->EnableFsr3Inputs.value_for_config()).c_str());
        ini.SetValue("Inputs", "EnableFfxInputs", GetBoolValue(Instance()->EnableFfxInputs.value_for_config()).c_str());
    }

    // V-Sync
    {
        ini.SetValue("V-Sync", "OverrideVsync", GetBoolValue(Instance()->OverrideVsync.value_for_config()).c_str());
        ini.SetValue("V-Sync", "ForceVsync", GetBoolValue(Instance()->ForceVsync.value_for_config()).c_str());
        ini.SetValue("V-Sync", "SyncInterval", GetIntValue(Instance()->VsyncInterval.value_for_config()).c_str());

        if (Instance()->VsyncInterval.has_value())
        {
            if (Instance()->VsyncInterval.value() < 0 || Instance()->VsyncInterval.value() > 3)
                Instance()->VsyncInterval.reset();
        }
    }

    auto pathWStr = absoluteFileName.wstring();

    LOG_INFO("Trying to save ini to: {0}", wstring_to_string(pathWStr));

    return ini.SaveFile(absoluteFileName.wstring().c_str()) >= 0;
}

bool Config::ReloadFakenvapi()
{
    std::lock_guard<std::mutex> lock(fakenvapiIniMutex);
    auto FN_iniPath = Util::DllPath().parent_path() / L"fakenvapi.ini";
    if (NvapiDllPath.has_value())
        FN_iniPath = std::filesystem::path(NvapiDllPath.value()).parent_path() / L"fakenvapi.ini";

    auto pathWStr = FN_iniPath.wstring();

    LOG_INFO("Trying to load fakenvapi's ini from: {0}", wstring_to_string(pathWStr));

    if (fakenvapiIni.LoadFile(FN_iniPath.c_str()) == SI_OK)
    {
        FN_EnableLogs = fakenvapiIni.GetLongValue("fakenvapi", "enable_logs", true);
        FN_EnableTraceLogs = fakenvapiIni.GetLongValue("fakenvapi", "enable_trace_logs", false);
        FN_ForceLatencyFlex = fakenvapiIni.GetLongValue("fakenvapi", "force_latencyflex", false);
        FN_LatencyFlexMode = fakenvapiIni.GetLongValue("fakenvapi", "latencyflex_mode", 0);
        FN_ForceReflex = fakenvapiIni.GetLongValue("fakenvapi", "force_reflex", 0);

        return true;
    }

    return false;
}

bool Config::SaveFakenvapiIni()
{
    auto FN_iniPath = Util::DllPath().parent_path() / L"fakenvapi.ini";
    if (NvapiDllPath.has_value())
        FN_iniPath = std::filesystem::path(NvapiDllPath.value()).parent_path() / L"fakenvapi.ini";

    auto pathWStr = FN_iniPath.wstring();

    LOG_INFO("Trying to save fakenvapi's ini to: {0}", wstring_to_string(pathWStr));

    fakenvapiIni.SetLongValue("fakenvapi", "enable_logs", FN_EnableLogs.value_or(true));
    fakenvapiIni.SetLongValue("fakenvapi", "enable_trace_logs", FN_EnableTraceLogs.value_or(false));
    fakenvapiIni.SetLongValue("fakenvapi", "force_latencyflex", FN_ForceLatencyFlex.value_or(false));
    fakenvapiIni.SetLongValue("fakenvapi", "latencyflex_mode", FN_LatencyFlexMode.value_or(0));
    fakenvapiIni.SetLongValue("fakenvapi", "force_reflex", FN_ForceReflex.value_or(0));

    StreamlineHooks::updateForceReflex();

    return fakenvapiIni.SaveFile(FN_iniPath.wstring().c_str()) >= 0;
}

bool Config::SaveXeFG()
{
    ini.SetValue("XeFG", "DepthInverted", GetBoolValue(Instance()->FGXeFGDepthInverted.value_for_config()).c_str());
    ini.SetValue("XeFG", "JitteredMV", GetBoolValue(Instance()->FGXeFGJitteredMV.value_for_config()).c_str());
    ini.SetValue("XeFG", "HighResMV", GetBoolValue(Instance()->FGXeFGHighResMV.value_for_config()).c_str());

    auto pathWStr = absoluteFileName.wstring();
    LOG_INFO("Trying to save ini to: {0}", wstring_to_string(pathWStr));

    return ini.SaveFile(absoluteFileName.wstring().c_str()) >= 0;
}

void Config::CheckUpscalerFiles()
{
    if (!State::Instance().nvngxExists)
        State::Instance().nvngxExists = std::filesystem::exists(Util::ExePath().parent_path() / L"nvngx.dll");

    if (!State::Instance().nvngxExists)
        State::Instance().nvngxExists = std::filesystem::exists(Util::ExePath().parent_path() / L"_nvngx.dll");

    if (!State::Instance().nvngxExists)
    {
        State::Instance().nvngxExists = GetModuleHandle(L"nvngx.dll") != nullptr;

        if (!State::Instance().nvngxExists)
            State::Instance().nvngxExists = GetModuleHandle(L"_nvngx.dll") != nullptr;

        if (State::Instance().nvngxExists)
            LOG_INFO("nvngx.dll found in memory");
        else
            LOG_WARN("nvngx.dll not found!");
    }
    else
    {
        LOG_INFO("nvngx.dll found in game folder");
    }

    if (auto nvngxReplacement = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx_dlss.dll");
        nvngxReplacement.has_value())
    {
        State::Instance().nvngxReplacement = nvngxReplacement.value().wstring();
    }

    State::Instance().libxessExists = std::filesystem::exists(Util::ExePath().parent_path() / L"libxess.dll");
    if (!State::Instance().libxessExists)
    {
        State::Instance().libxessExists = GetModuleHandle(L"libxess.dll") != nullptr;

        if (State::Instance().libxessExists)
            LOG_INFO("libxess.dll found in memory");
        else
            LOG_WARN("libxess.dll not found!");
    }
    else
    {
        LOG_INFO("libxess.dll found in game folder");
    }
}

std::vector<std::string> Config::GetConfigLog() { return _log; }

std::optional<std::string> Config::readString(std::string section, std::string key, bool lowercase)
{
    std::string value = ini.GetValue(section.c_str(), key.c_str(), "auto");

    std::string lower = value;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto")
        return std::nullopt;

    _log.push_back(std::format("{}.{}: {}", section, key, value));

    return lowercase ? lower : value;
}

std::optional<std::wstring> Config::readWString(std::string section, std::string key, bool lowercase)
{
    std::string value = ini.GetValue(section.c_str(), key.c_str(), "auto");

    std::string lower = value;
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "auto")
        return std::nullopt;

    _log.push_back(std::format("{}.{}: {}", section, key, value));

    return lowercase ? string_to_wstring(lower) : string_to_wstring(value);
}

std::optional<float> Config::readFloat(std::string section, std::string key)
{
    auto value = readString(section, key);

    try
    {
        float result;

        if (value.has_value() && isFloat(value.value(), result))
            return result;

        return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<int> Config::readInt(std::string section, std::string key)
{
    auto value = readString(section, key);
    if (!value.has_value())
        return std::nullopt;

    const auto& s = *value;
    try
    {
        size_t idx = 0;
        int result;

        // detect hex prefix
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
        {
            result = std::stoi(s, &idx, 16);
        }
        else
        {
            result = std::stoi(s, &idx, 10);
        }

        // ensure we consumed the whole string
        if (idx == s.size())
            return result;
        else
            return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out// out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<uint32_t> Config::readUInt(std::string section, std::string key)
{
    auto value = readString(section, key);
    if (!value.has_value())
        return std::nullopt;

    const auto& s = *value;
    try
    {
        size_t idx = 0;
        int result;

        // detect hex prefix
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
        {
            result = std::stoi(s, &idx, 16);
        }
        else
        {
            result = std::stoi(s, &idx, 10);
        }

        // ensure we consumed the whole string
        if (idx == s.size())
            return result;
        else
            return std::nullopt;
    }
    catch (const std::bad_optional_access&) // missing or auto value
    {
        return std::nullopt;
    }
    catch (const std::invalid_argument&) // invalid float string for std::stof
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&) // out// out of range for 32 bit float
    {
        return std::nullopt;
    }
}

std::optional<bool> Config::readBool(std::string section, std::string key)
{
    auto value = readString(section, key, true);
    if (value == "true")
        return true;
    else if (value == "false")
        return false;

    return std::nullopt;
}

Config* Config::Instance()
{
    // Meyers singleton - thread-safe by C++11 standard
    // No need for std::call_once or manual synchronization
    static Config instance;
    return &instance;
}
