#include "nvapp.h"

#define SER_APPNAME "name"
#define SER_APPID "id"
#define SER_APPGUID "guid"
#define SER_APPLASTSTART "lastappstarttime"
#define SER_APPPLATFORM "gameplatform"
#define SER_APPHDR "hdr"
#define SER_APPCOLLECTOR "appcollector"
#define SER_HIDDEN "hidden"
#define SER_DIRECTLAUNCH "directlaunch"
#define SER_APPLIST "apps"

NvApp::NvApp(const boost::property_tree::ptree& settings, int computerIndex, int appIndex)
{
    std::string prex = std::to_string(computerIndex) + '\\' + SER_APPLIST + '\\' + std::to_string(appIndex) + '\\';
    name = settings.get<std::string>(prex + SER_APPNAME);
    id = settings.get<int>(prex + SER_APPID);
    lastAppStartTime = settings.get<int>(prex + SER_APPLASTSTART);
    guid = settings.get<std::string>(prex + SER_APPGUID, "");
    gamePlatform = settings.get<std::string>(prex + SER_APPPLATFORM);
    hdrSupported = settings.get<bool>(prex + SER_APPHDR);
    isAppCollectorGame = settings.get<bool>(prex + SER_APPCOLLECTOR);
    hidden = settings.get<bool>(prex + SER_HIDDEN);
    directLaunch = settings.get<bool>(prex + SER_DIRECTLAUNCH);
}

void NvApp::serialize(boost::property_tree::ptree& settings, int computerIndex, int appIndex) const
{
    std::string prex = std::to_string(computerIndex) + '\\' + SER_APPLIST + '\\' + std::to_string(appIndex) + '\\';
    settings.put(prex + SER_APPNAME, name);
    settings.put(prex + SER_APPID, id);
    settings.put(prex + SER_APPLASTSTART, lastAppStartTime);
    settings.put(prex + SER_APPGUID, guid);
    settings.put(prex + SER_APPPLATFORM, gamePlatform);
    settings.put(prex + SER_APPHDR, hdrSupported);
    settings.put(prex + SER_APPCOLLECTOR, isAppCollectorGame);
    settings.put(prex + SER_HIDDEN, hidden);
    settings.put(prex + SER_DIRECTLAUNCH, directLaunch);
}
