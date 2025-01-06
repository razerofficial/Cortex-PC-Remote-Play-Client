#include "utils.h"
#include "configuer.h"
#include "streamingpreferences.h"
#include "backend/systemproperties.h"

#include <cmath>

StreamingPreferences::StreamingPreferences()
{
    reload();
}

StreamingPreferences* StreamingPreferences::get()
{
    static StreamingPreferences s_Prefs;
    return &s_Prefs;
}

void StreamingPreferences::reload()
{
    Configure* settings = Configure::getInstance();

    int defaultVer = settings->getGeneral<int>(SER_DEFAULTVER);

    virtualDisplay = settings->getGeneral<bool>(SER_VDM);
    customresolutionandfps = settings->getGeneral<bool>(SER_CRF);
    width = settings->getGeneral<int>(SER_WIDTH);
    height = settings->getGeneral<int>(SER_HEIGHT);
    fps = settings->getGeneral<int>(SER_FPS);
    bitrateKbps = settings->getGeneral<int>(SER_BITRATE);
    enableVsync = settings->getGeneral<bool>(SER_VSYNC);
    gameOptimizations = settings->getGeneral<bool>(SER_GAMEOPTS);
    playAudioOnHost = settings->getGeneral<bool>(SER_HOSTAUDIO);
    multiController = settings->getGeneral<bool>(SER_MULTICONT);
    enableMdns = settings->getGeneral<bool>(SER_MDNS);
    quitAppAfter = settings->getGeneral<bool>(SER_QUITAPPAFTER);
    absoluteMouseMode = settings->getGeneral<bool>(SER_ABSMOUSEMODE);
    absoluteTouchMode = settings->getGeneral<bool>(SER_ABSTOUCHMODE);
    framePacing = settings->getGeneral<bool>(SER_FRAMEPACING);
    connectionWarnings = settings->getGeneral<bool>(SER_CONNWARNINGS);
    richPresence = settings->getGeneral<bool>(SER_RICHPRESENCE);
    gamepadMouse = settings->getGeneral<bool>(SER_GAMEPADMOUSE);
    detectNetworkBlocking = settings->getGeneral<bool>(SER_DETECTNETBLOCKING);
    showPerformanceOverlay = settings->getGeneral<bool>(SER_SHOWPERFOVERLAY);
    packetSize = settings->getGeneral<int>(SER_PACKETSIZE);
    swapMouseButtons = settings->getGeneral<bool>(SER_SWAPMOUSEBUTTONS);
    muteOnFocusLoss = settings->getGeneral<bool>(SER_MUTEONFOCUSLOSS);
    backgroundGamepad = settings->getGeneral<bool>(SER_BACKGROUNDGAMEPAD);
    reverseScrollDirection = settings->getGeneral<bool>(SER_REVERSESCROLL);
    swapFaceButtons = settings->getGeneral<bool>(SER_SWAPFACEBUTTONS);
    keepAwake = settings->getGeneral<bool>(SER_KEEPAWAKE);
    enableHdr = settings->getGeneral<bool>(SER_HDR);
    virtualDisplayMode = static_cast<VirtualDisplayMode>(settings->getGeneral<int>(SER_VIRTUALDISPLAY));
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings->getGeneral<int>(SER_CAPTURESYSKEYS));
    audioConfig = static_cast<AudioConfig>(settings->getGeneral<int>(SER_AUDIOCFG));
    videoCodecConfig = static_cast<VideoCodecConfig>(settings->getGeneral<int>(SER_VIDEOCFG));
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings->getGeneral<int>(SER_VIDEODEC));
    windowMode = static_cast<WindowMode>(settings->getGeneral<int>(SER_WINDOWMODE));
    uiDisplayMode = static_cast<UIDisplayMode>(settings->getGeneral<int>(SER_UIDISPLAYMODE));
    language = static_cast<Language>(settings->getGeneral<int>(SER_LANGUAGE));
    terminateAppTime = settings->getGeneral<int>(SER_TERMINATEAPPTIME);


    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#if defined(__APPLE__) || defined(__MACH__)
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }
}

void StreamingPreferences::save()
{
    Configure* settings = Configure::getInstance();

    settings->setGeneral(SER_VDM, virtualDisplay);
    settings->setGeneral(SER_CRF, customresolutionandfps);
    settings->setGeneral(SER_WIDTH, width);
    settings->setGeneral(SER_HEIGHT, height);
    settings->setGeneral(SER_FPS, fps);
    settings->setGeneral(SER_BITRATE, bitrateKbps);
    settings->setGeneral(SER_VSYNC, enableVsync);
    settings->setGeneral(SER_GAMEOPTS, gameOptimizations);
    settings->setGeneral(SER_HOSTAUDIO, playAudioOnHost);
    settings->setGeneral(SER_MULTICONT, multiController);
    settings->setGeneral(SER_MDNS, enableMdns);
    settings->setGeneral(SER_QUITAPPAFTER, quitAppAfter);
    settings->setGeneral(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings->setGeneral(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings->setGeneral(SER_FRAMEPACING, framePacing);
    settings->setGeneral(SER_CONNWARNINGS, connectionWarnings);
    settings->setGeneral(SER_RICHPRESENCE, richPresence);
    settings->setGeneral(SER_GAMEPADMOUSE, gamepadMouse);
    settings->setGeneral(SER_PACKETSIZE, packetSize);
    settings->setGeneral(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings->setGeneral(SER_SHOWPERFOVERLAY, showPerformanceOverlay);
    settings->setGeneral(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings->setGeneral(SER_HDR, enableHdr);
    settings->setGeneral(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings->setGeneral(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings->setGeneral(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings->setGeneral(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings->setGeneral(SER_LANGUAGE, static_cast<int>(language));
    settings->setGeneral(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings->setGeneral(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings->setGeneral(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings->setGeneral(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings->setGeneral(SER_REVERSESCROLL, reverseScrollDirection);
    settings->setGeneral(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings->setGeneral(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings->setGeneral(SER_VIRTUALDISPLAY, virtualDisplayMode);
    settings->setGeneral(SER_KEEPAWAKE, keepAwake);
    settings->setGeneral(SER_TERMINATEAPPTIME, terminateAppTime);

    settings->saveGeneral();
}

void StreamingPreferences::modifySettings(const std::string& Settings)
{
    try
    {
        nlohmann::json modifySettings = nlohmann::json::parse(Settings);

        //video
        if (modifySettings.contains(SER_VDM))
        {
            this->virtualDisplay = modifySettings[SER_VDM].get<bool>();
        }
        if (modifySettings.contains(SER_VIRTUALDISPLAY))
        {
            this->virtualDisplayMode = StreamingPreferences::stringToVDMConfig(modifySettings[SER_VIRTUALDISPLAY].get<std::string>());
        }
        if (modifySettings.contains(SER_CRF))
        {
            this->customresolutionandfps = modifySettings[SER_CRF].get<bool>();
        }
        if (modifySettings.contains(SER_HEIGHT))
        {
            this->height = modifySettings[SER_HEIGHT].get<int>();
        }
        if (modifySettings.contains(SER_WIDTH))
        {
            this->width = modifySettings[SER_WIDTH].get<int>();
        }
        if (modifySettings.contains(SER_FPS))
        {
            this->fps = modifySettings[SER_FPS].get<int>();
        }
        if (modifySettings.contains(SER_BITRATE))
        {
            this->bitrateKbps = modifySettings[SER_BITRATE].get<int>();
        }
        if (modifySettings.contains(SER_WINDOWMODE))
        {
            this->windowMode = StreamingPreferences::stringToWindowModeConfig(modifySettings[SER_WINDOWMODE].get<std::string>());
        }
        if (modifySettings.contains(SER_FRAMEPACING))
        {
            this->framePacing = modifySettings[SER_FRAMEPACING].get<bool>();
        }
        if (modifySettings.contains(SER_HDR))
        {
            this->enableHdr = modifySettings[SER_HDR].get<bool>();
        }

        //audio
        if (modifySettings.contains(SER_AUDIOCFG))
        {
            this->audioConfig = StreamingPreferences::stringToAudioConfig(modifySettings[SER_AUDIOCFG].get<std::string>());
        }
        if (modifySettings.contains(SER_MUTEHOSTAUDIO))
        {
            this->playAudioOnHost = !modifySettings[SER_MUTEHOSTAUDIO].get<bool>(); // Invert because it's "mute host audio"
        }
        if (modifySettings.contains(SER_MUTEONFOCUSLOSS))
        {
            this->muteOnFocusLoss = modifySettings[SER_MUTEONFOCUSLOSS].get<bool>();
        }

        //input
        if (modifySettings.contains(SER_OPTIMIZEMOUSE))
        {
            this->absoluteMouseMode = modifySettings[SER_OPTIMIZEMOUSE].get<bool>();
        }
        if (modifySettings.contains(SER_VIRTUALTRACKPAD))
        {
            this->absoluteTouchMode = !modifySettings[SER_VIRTUALTRACKPAD].get<bool>();
        }

        //host
        if (modifySettings.contains(SER_GAMEOPTS))
        {
            this->gameOptimizations = modifySettings[SER_GAMEOPTS].get<bool>();
        }
        if (modifySettings.contains(SER_TERMINATEAPPTIME))
        {
            this->terminateAppTime = modifySettings[SER_TERMINATEAPPTIME].get<int>();
        }
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "modify settings error: " << e.what();
    }

    this->save();
}

void StreamingPreferences::resetSettings()
{
    NvDisplayMode primaryMonitor = SystemProperties::get()->primaryMonitorDisplayMode;

    this->virtualDisplay = false;
    this->virtualDisplayMode = StreamingPreferences::VDM_VIRTUAL_SCREEN_ONLY;
    this->customresolutionandfps = true;
    this->height = 0;
    this->width = 0;
    this->fps = 0;
    this->bitrateKbps = StreamingPreferences::getDefaultBitrate(primaryMonitor.width, primaryMonitor.height, primaryMonitor.refreshRate);
    this->windowMode = StreamingPreferences::WM_FULLSCREEN;
    this->framePacing = true;
    this->enableHdr = false;

    this->audioConfig = StreamingPreferences::AC_STEREO;
    this->playAudioOnHost = false;
    this->muteOnFocusLoss = false;

    this->absoluteMouseMode = false;
    this->absoluteTouchMode = true;

    this->gameOptimizations = true;
    this->terminateAppTime = 0;

    this->save();
    return;
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (std::sqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    return std::round(resolutionFactor * frameRateFactor) * 1000;
}

std::string StreamingPreferences::AudioConfigString(AudioConfig config)
{
    std::string string;
    switch (config)
    {
    case AC_STEREO:
        string = "Stereo";
        break;
    case AC_51_SURROUND:
        string = "5.1 surround sound";
        break;
    case AC_71_SURROUND:
        string = "7.1 surround sound";
        break;
    default:
        string = "unknown";
        break;
    }

    return string;
}

StreamingPreferences::AudioConfig StreamingPreferences::stringToAudioConfig(const std::string& str)
{
    if (str == "Stereo") {
        return AC_STEREO;
    } else if (str == "5.1 surround sound") {
        return AC_51_SURROUND;
    } else if (str == "7.1 surround sound") {
        return AC_71_SURROUND;
    } else {
        return AC_UNKNOWN;
    }
}

StreamingPreferences::WindowMode StreamingPreferences::stringToWindowModeConfig(const std::string& str)
{
    if (str == "Fullscreen") {
        return WM_FULLSCREEN;
    } else if (str == "Borderless windowed") {
        return WM_FULLSCREEN_DESKTOP;
    } else if (str == "Windowed") {
        return WM_WINDOWED;
    } else {
        return WM_UNKNOWN;
    }
}

std::string StreamingPreferences::VirtualDisplayModeString(VirtualDisplayMode mode)
{
    std::string string;
    switch (mode)
    {
    case VDM_SEPARATE_SCREEN:
        string = "separateScreen";
        break;
    case VDM_VIRTUAL_SCREEN_ONLY:
        string = "virtualScreenOnly";
        break;
    default:
        string = "unknown";
        break;
    }

    return string;
}

StreamingPreferences::VirtualDisplayMode StreamingPreferences::stringToVDMConfig(const std::string& str)
{
    if (str == "separateScreen") return VDM_SEPARATE_SCREEN;
    else if (str == "virtualScreenOnly") return VDM_VIRTUAL_SCREEN_ONLY;
    else return VDM_UNKNOWN;
}

std::string StreamingPreferences::WindowModeString(WindowMode mode)
{
    std::string string;
    switch (mode)
    {
    case WM_FULLSCREEN:
        string = "Fullscreen";
        break;
    case WM_FULLSCREEN_DESKTOP:
        string = "Borderless windowed";
        break;
    case WM_WINDOWED:
        string = "Windowed";
        break;
    default:
        string = "Unknown";
        break;
    }

    return string;
}
