#pragma once

#include "common.h"
#include "nvhttp.h"
#include <string>
#include <vector>
#include <SDL_rect.h>


class SystemProperties
{
    friend class QuerySdlVideoThread;
    friend class RefreshDisplaysThread;

public:
    static SystemProperties* get();

    void refreshDisplays();
    SDL_Rect getNativeResolution(int displayIndex);
    SDL_Rect getSafeAreaResolution(int displayIndex);
    int getRefreshRate(int displayIndex);

public:
    std::string deviceName;
    bool hasHardwareAcceleration;
    bool rendererAlwaysFullScreen;
    bool isRunningWayland;
    bool isRunningXWayland;
    bool isWow64;
    std::string friendlyNativeArchName;
    bool hasDesktopEnvironment;
    bool hasBrowser;
    bool hasDiscordIntegration;
    std::string unmappedGamepads;
    RazerSize maximumResolution;
    std::vector<SDL_Rect> monitorNativeResolutions;
    std::vector<SDL_Rect> monitorSafeAreaResolutions;
    std::vector<int> monitorRefreshRates;
    NvDisplayMode primaryMonitorDisplayMode;
    std::string versionString;
    bool supportsHdr;
    bool usesMaterial3Theme;

private:
    SystemProperties();
    void querySdlVideoInfo();
    void querySdlVideoInfoInternal();
    void refreshDisplaysInternal();
};

