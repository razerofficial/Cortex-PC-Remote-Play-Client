#include "configuer.h"
#include "utils.h"
#include "streamingpreferences.h"
#include "backend/systemproperties.h"

#include <boost/property_tree/ini_parser.hpp>

static const nlohmann::json defaultGeneral = nlohmann::json::parse(R"({
"defaultver": 2,"virtualdisplay": false,"customresolutionandfps": true,
"width": 0, "height": 0, "fps": 0, "bitrate": 0, "vsync": true,
"gameopts": true, "hostaudio": false, "multicontroller": true, "mdns": true,
"quitAppAfter": false, "mouseacceleration": false, "abstouchmode": true,
"framepacing": true, "connwarnings": true, "richpresence": true, 
"gamepadmouse": true, "detectnetblocking": true, "showperfoverlay": false,
"packetsize": 0, "swapmousebuttons": false, "muteonfocusloss": false,
"backgroundgamepad": false, "reversescroll": false, "swapfacebuttons": false,
"keepawake": true, "hdr": false, "virtualdisplaymode": 2, "capturesyskeys": 1,
"audiocfg": 0, "videocfg": 0, "videodec": 1, "windowmode": 0, "uidisplaymode": 0,
"language": 0, "uihttpport": 51343, "terminateAppTime": 0, "certificate": "",
"key": ""})");

Configure* Configure::getInstance()
{
    static Configure s_instance;
    return &s_instance;
}

Configure::Configure()
{
    bool ok;
    std::string localAppData = Environment::environmentVariableStringValue("LOCALAPPDATA", &ok);
    if (!ok)
        LOG(ERROR) << "Failed to get environment variable LOCALAPPDATA!";

    std::filesystem::path path = localAppData + "\\Razer\\Razer Cortex\\RemotePlay\\Client";
    if (!std::filesystem::exists(path))
    {
        try
        {
            std::filesystem::create_directories(path);
        }
        catch (const std::filesystem::filesystem_error& ex) 
        {
            LOG(ERROR) << "Error creating directory: " << ex.what();
        }
    }

    m_generalPath = path.string() + "\\general.json";
    if (!std::filesystem::exists(m_generalPath))
    {
        int recommendedFullScreenMode;
#if defined(__APPLE__) && defined(__MACH__)
        recommendedFullScreenMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
#else
        // Wayland doesn't support modesetting, so use fullscreen desktop mode.
        if (WMUtils::isRunningWayland()) {
            recommendedFullScreenMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
        }
        else {
            recommendedFullScreenMode = StreamingPreferences::WM_FULLSCREEN;
        }
#endif
        setGeneral(SER_DEFAULTVER, 0);
        setGeneral(SER_VDM, false);
        setGeneral(SER_CRF, true);
        setGeneral(SER_WIDTH, 0);
        setGeneral(SER_HEIGHT, 0);
        setGeneral(SER_FPS, 0);
        NvDisplayMode primaryMonitor = SystemProperties::get()->primaryMonitorDisplayMode;
        int bitrate = StreamingPreferences::getDefaultBitrate(primaryMonitor.width, 
            primaryMonitor.height, primaryMonitor.refreshRate);
        setGeneral(SER_BITRATE, bitrate);
        setGeneral(SER_VSYNC, true);
        setGeneral(SER_GAMEOPTS, true);
        setGeneral(SER_HOSTAUDIO, false);
        setGeneral(SER_MULTICONT, true);
        setGeneral(SER_MDNS, true);
        setGeneral(SER_QUITAPPAFTER, false);
        setGeneral(SER_ABSMOUSEMODE, false);
        setGeneral(SER_ABSTOUCHMODE, true);
        setGeneral(SER_FRAMEPACING, true);
        setGeneral(SER_CONNWARNINGS, true);
        setGeneral(SER_RICHPRESENCE, true);
        setGeneral(SER_GAMEPADMOUSE, true);
        setGeneral(SER_DETECTNETBLOCKING, true);
        setGeneral(SER_SHOWPERFOVERLAY, false);
        setGeneral(SER_PACKETSIZE, 0);
        setGeneral(SER_SWAPMOUSEBUTTONS, false);
        setGeneral(SER_MUTEONFOCUSLOSS, false);
        setGeneral(SER_BACKGROUNDGAMEPAD, false);
        setGeneral(SER_REVERSESCROLL, false);
        setGeneral(SER_SWAPFACEBUTTONS, false);
        setGeneral(SER_KEEPAWAKE, true);
        setGeneral(SER_HDR, false);
        setGeneral(SER_VIRTUALDISPLAY, static_cast<int>(StreamingPreferences::VDM_VIRTUAL_SCREEN_ONLY));
        setGeneral(SER_CAPTURESYSKEYS, static_cast<int>(StreamingPreferences::CSK_FULLSCREEN));
        setGeneral(SER_AUDIOCFG, static_cast<int>(StreamingPreferences::AC_STEREO));
        setGeneral(SER_VIDEOCFG, static_cast<int>(StreamingPreferences::VCC_AUTO));
        setGeneral(SER_VIDEODEC, static_cast<int>(StreamingPreferences::VDS_FORCE_HARDWARE));
        setGeneral(SER_WINDOWMODE, recommendedFullScreenMode);
        setGeneral(SER_UIDISPLAYMODE, static_cast<int>(StreamingPreferences::UI_WINDOWED));
        setGeneral(SER_LANGUAGE, static_cast<int>(StreamingPreferences::LANG_AUTO));
        setGeneral(SER_UIHTTPPORT, 51343);
        setGeneral(SER_TERMINATEAPPTIME, 0);
        setGeneral(SER_CERT, "");
        setGeneral(SER_KEY, "");

        saveGeneral();
    }
    else
    {
        //read general config file
        std::ifstream file(m_generalPath.string());
        if (!file.is_open())
            LOG(ERROR) << "Could not open file: " << m_generalPath.string();

        try
        {
            file >> m_general;

            bool update = false;
            for (auto& [key, value] : defaultGeneral.items())
            {
                if (!m_general.contains(key))
                {
                    update = true;
                    m_general[key] = value;
                }
            }
            if (update)
                saveGeneral();
        }
        catch (const nlohmann::json::parse_error& e)
        {
            LOG(ERROR) << "general.json JSON parse error: : " << e.what();
        }
        catch (const std::exception& e)
        {
            LOG(ERROR) << "Error reading JSON from general.json : " << e.what();
        }
    }

    m_hostsPath = path.string() + "\\hosts.ini";
    if (std::filesystem::exists(m_hostsPath))
    {
        //read hosts config file
        try
        {
            read_ini(m_hostsPath.string(), m_hosts);
        }
        catch (const std::exception& ex)
        {
            LOG(FATAL) << "Read config file error! " << ex.what();
        }
    }
}

void Configure::saveGeneral()
{
    std::ofstream file(m_generalPath.string());
    if (!file.is_open())
        LOG(ERROR) << "Could not open file: " << m_generalPath.string();

    try 
    {
        file << std::setw(4) << m_general << std::endl;
    }
    catch (const std::exception& e)
    {
        LOG(ERROR) << "Error writing JSON to general.json : " << e.what();
    }
}

void Configure::saveHosts()
{
    try
    {
        boost::property_tree::write_ini(m_hostsPath.string(), m_hosts);
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Error writing to host config file: " << e.what();
    }
}

int Configure::getHostsSize()
{
    try
    {
        return m_hosts.get<int>("size", 0);
    }
    catch (const boost::property_tree::ptree_bad_path& e)
    {
        LOG(ERROR) << "host size not found: " << e.what();
        return 0;
    }
    catch (const boost::property_tree::ptree_bad_data& e)
    {
        LOG(ERROR) << "host size Data type mismatch: " << e.what();
        return 0;
    }
}

void Configure::setHostsSize(int size)
{
    m_hosts.put("size", size);
}

boost::property_tree::ptree Configure::getHosts()
{
    return m_hosts;
}

void Configure::setHosts(const boost::property_tree::ptree& hosts)
{
    m_hosts = hosts;
}
