#include "settings.h"
#include "common.h"
#include "settings/streamingpreferences.h"
#include "backend/systemproperties.h"
#include "backend/windowsmessage.h"
#include "mainwindow.h"

#include <json.hpp>
#include <glog/logging.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>


std::string Settings::getSettings()
{
    StreamingPreferences* preference = StreamingPreferences::get();

    nlohmann::json jsonSettings;
    //video
    jsonSettings[SER_VDM] = preference->virtualDisplay;
    jsonSettings[SER_VIRTUALDISPLAY] = StreamingPreferences::VirtualDisplayModeString(preference->virtualDisplayMode);
    jsonSettings[SER_CRF] = preference->customresolutionandfps;
    jsonSettings[SER_HEIGHT] = preference->height;
    jsonSettings[SER_WIDTH] = preference->width;
    jsonSettings[SER_FPS] = preference->fps;
    jsonSettings[SER_BITRATE] = preference->bitrateKbps;
    jsonSettings[SER_WINDOWMODE] = StreamingPreferences::WindowModeString(preference->windowMode);
    jsonSettings[SER_FRAMEPACING] = preference->framePacing;
    jsonSettings[SER_HDR] = preference->enableHdr;

    //audio
    jsonSettings[SER_AUDIOCFG] = StreamingPreferences::AudioConfigString(preference->audioConfig);
    jsonSettings[SER_MUTEHOSTAUDIO] = !preference->playAudioOnHost;
    jsonSettings[SER_MUTEONFOCUSLOSS] = preference->muteOnFocusLoss;

    //input
    jsonSettings[SER_OPTIMIZEMOUSE] = preference->absoluteMouseMode;
    jsonSettings[SER_VIRTUALTRACKPAD] = !preference->absoluteTouchMode;

    //host
    jsonSettings[SER_GAMEOPTS] = preference->gameOptimizations;
    jsonSettings[SER_TERMINATEAPPTIME] = preference->terminateAppTime;

    return jsonSettings.dump();
}

void Settings::modifySettings(const std::string& Settings)
{
    SettingMessage* msg = new SettingMessage(Settings);
    extern MyWindow g_window;
    HWND hWnd = g_window.getHandle();
    PostMessage(hWnd, msg->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(msg));
}

void Settings::resetSettings()
{
    SettingMessage* msg = new SettingMessage();
    extern MyWindow g_window;
    HWND hWnd = g_window.getHandle();
    PostMessage(hWnd, msg->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(msg));
}

std::string Settings::getScreenInfo()
{
    std::vector<nlohmann::json> resolutionListJson;
    std::vector<ResolutionModel> resolutionList = initializeresolution();
    for (const ResolutionModel& e : resolutionList)
    {
        resolutionListJson.push_back({ {"width", e.videoWidth}, {"height", e.videoHeight} });
    }
    
    nlohmann::json fpsListJosn;
    std::vector<FPSModel> fpsList = initializeFPS();
    for (const FPSModel& e : fpsList)
    {
        fpsListJosn.push_back(e.videoFPS);
    }

    nlohmann::json adapterJson;
    NvDisplayMode displayMode = SystemProperties::get()->primaryMonitorDisplayMode;
    adapterJson["adapterWidth"] = displayMode.width;
    adapterJson["adapterHeight"] = displayMode.height;
    adapterJson["adapterFPS"] = displayMode.refreshRate;

    bool supportsHdr = SystemProperties::get()->supportsHdr;

    nlohmann::json screenInfo;
    screenInfo["resolutionList"] = resolutionListJson;
    screenInfo["fpsList"] = fpsListJosn;
    screenInfo["adapter"] = adapterJson;
    screenInfo["supportsHdr"] = supportsHdr;

    return screenInfo.dump();
}

int Settings::addRefreshRateOrdered(std::vector<FPSModel>& fpsList, int refreshRate, bool custom)
{
    int indexToAdd = 0;
    for (int j = 0; j < fpsList.size(); j++)
    {
        int existing_fps = fpsList.at(j).videoFPS;

        if (refreshRate == existing_fps || (custom && fpsList.at(j).isCustom))
        {
            // Duplicate entry, skip
            indexToAdd = -1;
            break;
        }
        else if (refreshRate > existing_fps)
        {
            // Candidate entrypoint after this entry
            indexToAdd = j + 1;
        }
    }

    // Insert this frame rate if it's not a duplicate
    if (indexToAdd >= 0)
    {
        // Custom values always go at the end of the list
        if (custom)
        {
            indexToAdd = fpsList.size();
        }

        fpsList.insert(fpsList.begin() + indexToAdd, FPSModel(refreshRate, custom));
    }

    return indexToAdd;
}

std::vector<FPSModel> Settings::initializeFPS()
{
    std::vector<FPSModel> fpsList;
    addRefreshRateOrdered(fpsList, 30, false);
    addRefreshRateOrdered(fpsList, 60, false);

    // Add native refresh rate for all attached displays
    bool done = false;
    for (int displayIndex = 0; !done; displayIndex++) 
    {
        int refreshRate = SystemProperties::get()->getRefreshRate(displayIndex);
        if (refreshRate == 0)
        {
            // Exceeded max count of displays
            done = true;
            break;
        }

        addRefreshRateOrdered(fpsList, refreshRate, false);
    }

    return fpsList;
}

void Settings::addDetectedResolution(std::vector<ResolutionModel>& resolutionList, SDL_Rect rect)
{
    int indexToAdd = 0;
    for (int j = 0; j < resolutionList.size(); j++)
    {
        int existingWidth = resolutionList.at(j).videoWidth;
        int existingHeight = resolutionList.at(j).videoHeight;

        if (rect.w == existingWidth && rect.h == existingHeight)
        {
            // Duplicate entry, skip
            indexToAdd = -1;
            break;
        }
        else if (rect.w * rect.h > existingWidth * existingHeight)
        {
            // Candidate entrypoint after this entry
            indexToAdd = j + 1;
        }
    }

    // Insert this display's resolution if it's not a duplicate
    if (indexToAdd >= 0)
    {
        resolutionList.insert(resolutionList.begin() + indexToAdd, ResolutionModel(rect.w, rect.h, false));
    }
}

std::vector<ResolutionModel> Settings::initializeresolution()
{
    std::vector<ResolutionModel> resolutionList;
    addDetectedResolution(resolutionList, SDL_Rect(0, 0, 1280, 720));
    addDetectedResolution(resolutionList, SDL_Rect(0, 0, 1920, 1080));
    addDetectedResolution(resolutionList, SDL_Rect(0, 0, 2560, 1440));
    addDetectedResolution(resolutionList, SDL_Rect(0, 0, 3840, 2160));

    // Refresh display data before using it to build the list
    SystemProperties::get()->refreshDisplays();

    // Add native and safe area resolutions for all attached displays
    bool done = false;
    for (int displayIndex = 0; !done; displayIndex++) 
    {
        SDL_Rect screenRect = SystemProperties::get()->getNativeResolution(displayIndex);
        SDL_Rect safeAreaRect = SystemProperties::get()->getSafeAreaResolution(displayIndex);

        if (screenRect.w == 0)
        {
            // Exceeded max count of displays
            done = true;
            break;
        }

        addDetectedResolution(resolutionList, screenRect);
        addDetectedResolution(resolutionList, safeAreaRect);
    }

    // Prune resolutions that are over the decoder's maximum
    int max_pixels = SystemProperties::get()->maximumResolution.width * SystemProperties::get()->maximumResolution.height;
    if (max_pixels > 0) 
    {
        auto it = resolutionList.begin();
        while (it != resolutionList.end())
        {
            if (it->videoWidth * it->videoHeight > max_pixels)
            {
                it = resolutionList.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return resolutionList;
}