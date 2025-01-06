#pragma once

#include <vector>
#include <string>

#include <json.hpp>
#include "common.h"

struct SDL_Rect;

class Settings
{
public:
    std::string getSettings();
    void modifySettings(const std::string& modifySettings);
    void resetSettings();

    std::string getScreenInfo();

private:
    int addRefreshRateOrdered(std::vector<FPSModel>& fpsList, int refreshRate, bool custom);
    std::vector<FPSModel> initializeFPS();

    void addDetectedResolution(std::vector<ResolutionModel>& resolutionList, SDL_Rect rect);
    std::vector<ResolutionModel> initializeresolution();

private:
    static std::vector<std::string> settingItems;

};
