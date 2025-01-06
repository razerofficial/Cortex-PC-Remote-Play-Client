#pragma once

#include "common.h"
#include <string>
#include <json.hpp>

class StreamingPreferences
{

public:
    static StreamingPreferences* get();

    static int
    getDefaultBitrate(int width, int height, int fps);

    void save();

    void reload();

    void modifySettings(const std::string& Settings);

    void resetSettings();

    enum AudioConfig
    {
        AC_STEREO,
        AC_51_SURROUND,
        AC_71_SURROUND,
        AC_UNKNOWN
    };

    enum VideoCodecConfig
    {
        VCC_AUTO,
        VCC_FORCE_H264,
        VCC_FORCE_HEVC,
        VCC_FORCE_HEVC_HDR_DEPRECATED, // Kept for backwards compatibility
        VCC_FORCE_AV1,
        VCC_UNKNOWN
    };

    enum VideoDecoderSelection
    {
        VDS_AUTO,
        VDS_FORCE_HARDWARE,
        VDS_FORCE_SOFTWARE,
        VDS_UNKNOWN
    };

    enum WindowMode
    {
        WM_FULLSCREEN,
        WM_FULLSCREEN_DESKTOP,
        WM_WINDOWED,
        WM_UNKNOWN
    };

    enum UIDisplayMode
    {
        UI_WINDOWED,
        UI_MAXIMIZED,
        UI_FULLSCREEN,
        UI_UNKNOWN
    };

    // New entries must go at the end of the enum
    // to avoid renumbering existing entries (which
    // would affect existing user preferences).
    enum Language
    {
        LANG_AUTO,
        LANG_EN,
        LANG_FR,
        LANG_ZH_CN,
        LANG_DE,
        LANG_NB_NO,
        LANG_RU,
        LANG_ES,
        LANG_JA,
        LANG_VI,
        LANG_TH,
        LANG_KO,
        LANG_HU,
        LANG_NL,
        LANG_SV,
        LANG_TR,
        LANG_UK,
        LANG_ZH_TW,
        LANG_PT,
        LANG_PT_BR,
        LANG_EL,
        LANG_IT,
        LANG_HI,
        LANG_PL,
        LANG_CS,
        LANG_HE,
        LANG_CKB,
    };

    enum CaptureSysKeysMode
    {
        CSK_OFF,
        CSK_FULLSCREEN,
        CSK_ALWAYS,
        CSK_UNKNOWN
    };

    enum VirtualDisplayMode
    {
        VDM_UNKNOWN,
        VDM_SEPARATE_SCREEN,
        VDM_VIRTUAL_SCREEN_ONLY,
    };

    static std::string AudioConfigString(AudioConfig config);
    static AudioConfig stringToAudioConfig(const std::string& str);

    static std::string WindowModeString(WindowMode mode);
    static WindowMode stringToWindowModeConfig(const std::string& str);

    static std::string VirtualDisplayModeString(VirtualDisplayMode mode);
    static VirtualDisplayMode stringToVDMConfig(const std::string& str);

    // Directly accessible members for preferences
    bool virtualDisplay;
    bool customresolutionandfps;
    int width;
    int height;
    int fps;
    int bitrateKbps;
    bool enableVsync;
    bool gameOptimizations;
    bool playAudioOnHost;
    bool multiController;
    bool enableMdns;
    bool quitAppAfter;
    bool absoluteMouseMode;
    bool absoluteTouchMode;
    bool framePacing;
    bool connectionWarnings;
    bool richPresence;
    bool gamepadMouse;
    bool detectNetworkBlocking;
    bool showPerformanceOverlay;
    bool swapMouseButtons;
    bool muteOnFocusLoss;
    bool backgroundGamepad;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    bool keepAwake;
    int packetSize;
    AudioConfig audioConfig;
    VideoCodecConfig videoCodecConfig;
    bool enableHdr;
    VideoDecoderSelection videoDecoderSelection;
    WindowMode windowMode;
    WindowMode recommendedFullScreenMode;
    UIDisplayMode uiDisplayMode;
    Language language;
    CaptureSysKeysMode captureSysKeysMode;
    VirtualDisplayMode virtualDisplayMode;
    int terminateAppTime;

private:
    explicit StreamingPreferences();
};

