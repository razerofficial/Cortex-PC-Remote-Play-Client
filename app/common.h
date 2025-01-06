#pragma once

#include <string>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_VDM "virtualdisplay"
#define SER_CRF "customresolutionandfps"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MUTEHOSTAUDIO "mutehostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_OPTIMIZEMOUSE "optimizemouse"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_VIRTUALTRACKPAD "virtualtrackpad"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_VIRTUALDISPLAY "virtualdisplaymode"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_UIHTTPPORT "uihttpport"
#define SER_TERMINATEAPPTIME "terminateAppTime"
#define SER_CERT "certificate"
#define SER_KEY "key"

#define CURRENT_DEFAULT_VER 2

const std::string programName = "RemotePlayClient";
const std::string version = "1.0.1.11";

struct RazerSize
{
    RazerSize():width(0), height(0){}
    RazerSize(int _width, int _height):width(_width), height(_height){}
    int width;
    int height;
};

struct FPSModel
{
    FPSModel(int _videoFPS, bool _isCustom) : videoFPS(_videoFPS), isCustom(_isCustom) {}

    int videoFPS;
    bool isCustom;
};

struct ResolutionModel
{
    ResolutionModel(int _videoWidth, int _videoHeight, bool _isCustom) : videoWidth(_videoWidth), videoHeight(_videoHeight), isCustom(_isCustom) {}

    int videoWidth;
    int videoHeight;
    bool isCustom;
};
