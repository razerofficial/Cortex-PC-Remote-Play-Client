#pragma once

#include <boost/property_tree/ptree.hpp>

class NvApp
{
public:
    NvApp() {}
    explicit NvApp(const boost::property_tree::ptree& settings, int computerIndex, int appIndex);

    bool operator==(const NvApp& other) const
    {
        return id == other.id &&
                /*lastAppStartTime == other.lastAppStartTime &&*/
                guid == other.guid &&
                name == other.name &&
                /*boxArt == other.boxArt &&*/
                gamePlatform == other.gamePlatform &&
                hdrSupported == other.hdrSupported &&
                isAppCollectorGame == other.isAppCollectorGame &&
                hidden == other.hidden &&
                directLaunch == other.directLaunch;
    }

    bool operator!=(const NvApp& other) const
    {
        return !operator==(other);
    }

    bool isInitialized()
    {
        return id != 0 && !name.empty();
    }

    void serialize(boost::property_tree::ptree& settings, int computerIndex, int appIndex) const;

    int id = 0;
    int64_t lastAppStartTime = 0;
    std::string guid;
    std::string name;
    std::string boxArt;
    std::string gamePlatform;
    bool hdrSupported = false;
    bool isAppCollectorGame = false;
    bool hidden = false;
    bool directLaunch = false;
};
