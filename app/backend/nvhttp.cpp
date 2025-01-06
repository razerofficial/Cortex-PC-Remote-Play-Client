#include "nvcomputer.h"
#include "identitymanager.h"
#include "systemproperties.h"
#include "settings/streamingpreferences.h"
#include "utils.h"
#include "razer.h"
#include "computermanager.h"

#include <regex>
#include <glog/logging.h>
#include <Limelight.h>
#include <Simple-Web-Server/client_https.hpp>
#include <Simple-Web-Server/client_http.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/algorithm/hex.hpp>

using HttpsClient = SimpleWeb::Client<SimpleWeb::HTTPS>;
using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;

#define FAST_FAIL_TIMEOUT_MS 2000
#define REQUEST_TIMEOUT_MS 5000
#define LAUNCH_TIMEOUT_MS 30000
#define RESUME_TIMEOUT_MS 30000
#define QUIT_TIMEOUT_MS 30000

NvHTTP::NvHTTP(NvAddress address, uint16_t httpsPort, std::string serverCert) :
    m_stop(false),
    m_ServerCert(serverCert)
{
    m_BaseUrlHttp.setScheme("http");
    m_BaseUrlHttps.setScheme("https");

    setAddress(address);
    setHttpsPort(httpsPort);
}

NvHTTP::NvHTTP(NvComputer* computer) :
    NvHTTP(computer->activeAddress, computer->activeHttpsPort, computer->serverCert)
{
}

void NvHTTP::setServerCert(std::string serverCert)
{
    m_ServerCert = serverCert;
}

void NvHTTP::setAddress(NvAddress address)
{
    assert(!address.isNull());

    m_Address = address;

    m_BaseUrlHttp.setHost(address.address());
    m_BaseUrlHttps.setHost(address.address());

    m_BaseUrlHttp.setPort(address.port());
}

void NvHTTP::setHttpsPort(uint16_t port)
{
    m_BaseUrlHttps.setPort(port);
}

NvAddress NvHTTP::address()
{
    return m_Address;
}

std::string NvHTTP::serverCert()
{
    return m_ServerCert;
}

uint16_t NvHTTP::httpPort()
{
    return m_BaseUrlHttp.port();
}

uint16_t NvHTTP::httpsPort()
{
    return m_BaseUrlHttps.port();
}

std::vector<int>
NvHTTP::parseQuad(std::string quad)
{
    std::vector<int> ret;

    // Return an empty vector for old GFE versions
    // that were missing GfeVersion.
    if (quad.empty()) {
        return ret;
    }

    std::vector<std::string> tmp;
    boost::split(tmp, quad, boost::is_any_of("."));
    ret.reserve(tmp.size());
    int num;
    for (int i = 0; i < tmp.size(); i++)
    {
        try
        {
            num = std::stoi(tmp.at(i));
        }
        catch (...)
        {
            num = 0;
        }
        ret.push_back(num);
    }

    return ret;
}

int
NvHTTP::getCurrentGame(std::string serverInfo)
{
    // GFE 2.8 started keeping currentgame set to the last game played. As a result, it no longer
    // has the semantics that its name would indicate. To contain the effects of this change as much
    // as possible, we'll force the current game to zero if the server isn't in a streaming session.
    std::string serverState = getXmlString(serverInfo, "state");
    if (!serverState.empty() && WMUtils::endsWith(serverState, "_SERVER_BUSY"))
    {
        std::string currentgame = getXmlString(serverInfo, "currentgame");
        int iCurrentgame;
        try
        {
            iCurrentgame = std::stoi(currentgame);
        }
        catch (...)
        {
            iCurrentgame = 0;
        }

        return iCurrentgame;
    }
    else
    {
        return 0;
    }
}

std::string
NvHTTP::getServerInfo(NvLogLevel logLevel, bool fastFail)
{
    std::string serverInfo;

    // Check if we have a pinned cert and HTTPS port for this host yet
    if (!m_ServerCert.empty() && httpsPort() != 0)
    {
        try
        {
            // Always try HTTPS first, since it properly reports
            // pairing status (and a few other attributes).
            serverInfo = openConnectionToString(m_BaseUrlHttps,
                                                "serverinfo",
                                                "razer_uuid=" + Razer::getInstance()->getUUID(),
                                                fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS);

            // Throws if the request failed
            verifyResponseStatus(serverInfo);
        }
        catch (const GfeHttpResponseException& e)
        {
            if (e.getStatusCode() == 401)
            {
                // Certificate validation error, fallback to HTTP
                serverInfo = openConnectionToString(m_BaseUrlHttp,
                                                    "serverinfo",
                                                    "razer_uuid=" + Razer::getInstance()->getUUID(),
                                                    fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS);
                verifyResponseStatus(serverInfo);
            }
            else
            {
                // Rethrow real errors
                throw e;
            }
        }
    }
    else
    {
        // Only use HTTP prior to pairing or fetching HTTPS port
        serverInfo = openConnectionToString(m_BaseUrlHttp,
                                            "serverinfo",
                                            "razer_uuid=" + Razer::getInstance()->getUUID(),
                                            fastFail ? FAST_FAIL_TIMEOUT_MS : REQUEST_TIMEOUT_MS);
        verifyResponseStatus(serverInfo);

        // Populate the HTTPS port
        std::string httpsPortString = getXmlString(serverInfo, "HttpsPort");
        uint16_t httpsPort;
        try {
            httpsPort = std::stoi(httpsPortString);
        } catch (...) {
            httpsPort = 0;
        }

        if (httpsPort == 0) {
            httpsPort = DEFAULT_HTTPS_PORT;
        }
        setHttpsPort(httpsPort);

        // If we just needed to determine the HTTPS port, we'll try again over
        // HTTPS now that we have the port number
        if (!m_ServerCert.empty()) {
            return getServerInfo(logLevel, fastFail);
        }
    }

    return serverInfo;
}

void
NvHTTP::startApp(std::string verb,
                 bool isGfe,
                 int appId,
                 PSTREAM_CONFIGURATION streamConfig,
                 bool sops,
                 bool localAudio,
                 int gamepadMask,
                 bool persistGameControllersOnDisconnect,
                 std::string& rtspSessionUrl)
{
    int riKeyId;

    memcpy(&riKeyId, streamConfig->remoteInputAesIv, sizeof(riKeyId));
    riKeyId = boost::endian::big_to_native(riKeyId);

    std::string aesKey(streamConfig->remoteInputAesKey, sizeof(streamConfig->remoteInputAesKey));
    std::string aesKeyHex = StringUtils::stringToHex(aesKey);

    std::string virtualDisplay;
    std::string virtualDisplayMode;
    StreamingPreferences* prefs = StreamingPreferences::get();
    if (prefs->virtualDisplay)
    {
        switch (prefs->virtualDisplayMode) 
        {
        case StreamingPreferences::VDM_SEPARATE_SCREEN:
            virtualDisplay = std::to_string(StreamingPreferences::VDM_SEPARATE_SCREEN);
            break;
        case StreamingPreferences::VDM_VIRTUAL_SCREEN_ONLY:
            virtualDisplay = std::to_string(StreamingPreferences::VDM_VIRTUAL_SCREEN_ONLY);
            break;
        default:
            virtualDisplay = std::to_string(StreamingPreferences::VDM_UNKNOWN);
            break;
        }

        NvDisplayMode mode = SystemProperties::get()->primaryMonitorDisplayMode;
        std::stringstream ss;
        ss << mode.width << 'x' << mode.height << 'x' << mode.refreshRate;
        virtualDisplayMode = ss.str();
    }

    std::string timeToTerminateApp = std::to_string(prefs->terminateAppTime);

    std::string response =
            openConnectionToString(m_BaseUrlHttps,
                                   verb,
                                   "appid="+std::to_string(appId)+
                                   "&mode="+std::to_string(streamConfig->width)+"x"+
                                   std::to_string(streamConfig->height)+"x"+
                                   // Using an FPS value over 60 causes SOPS to default to 720p60,
                                   // so force it to 0 to ensure the correct resolution is set. We
                                   // used to use 60 here but that locked the frame rate to 60 FPS
                                   // on GFE 3.20.3. We don't need this hack for Sunshine.
                                   std::to_string((streamConfig->fps > 60 && isGfe) ? 0 : streamConfig->fps)+
                                   "&additionalStates=1&sops="+std::to_string(sops ? 1 : 0)+
                                   "&rikey="+aesKeyHex+
                                   "&rikeyid="+std::to_string(riKeyId)+
                                   ((streamConfig->supportedVideoFormats & VIDEO_FORMAT_MASK_10BIT) ?
                                   "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0" :
                                   "")+
                                   "&localAudioPlayMode="+std::to_string(localAudio ? 1 : 0)+
                                   "&surroundAudioInfo="+std::to_string(SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(streamConfig->audioConfiguration))+
                                   "&remoteControllersBitmap="+std::to_string(gamepadMask)+
                                   "&gcmap="+std::to_string(gamepadMask)+
                                   "&gcpersist="+std::to_string(persistGameControllersOnDisconnect ? 1 : 0)+
                                   "&devicenickname="+SystemProperties::get()->deviceName+
                                   "&virtualDisplay="+virtualDisplay+"&virtualDisplayMode="+virtualDisplayMode+
                                   "&timeToTerminateApp="+timeToTerminateApp+
                                   LiGetLaunchUrlQueryParameters(),
                                   LAUNCH_TIMEOUT_MS);

    LOG(INFO) << "Launch response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    rtspSessionUrl = getXmlString(response, "sessionUrl0");
}

void
NvHTTP::quitApp()
{
    std::string response =
            openConnectionToString(m_BaseUrlHttps,
                                   "cancel",
                                   "",
                                   QUIT_TIMEOUT_MS);

    LOG(INFO) << "Quit response:" << response;

    // Throws if the request failed
    verifyResponseStatus(response);

    // Newer GFE versions will just return success even if quitting fails
    // if we're not the original requester.
    if (getCurrentGame(getServerInfo(NvHTTP::NVLL_ERROR)) != 0) {
        // Generate a synthetic GfeResponseException letting the caller know
        // that they can't kill someone else's stream.
        throw GfeHttpResponseException(599, "");
    }
}

std::vector<NvDisplayMode>
NvHTTP::getDisplayModeList(std::string serverInfo)
{
    std::vector<NvDisplayMode> modes;

    std::stringstream ss(serverInfo);
    boost::property_tree::ptree pt;

    read_xml(ss, pt);

    // 遍历 <App> 节点
    BOOST_FOREACH(const boost::property_tree::ptree::value_type &v, pt.get_child("root"))
    {
        if (v.first == "DisplayMode") {
            int width = v.second.get<int>("Width", 0);
            int height = v.second.get<int>("Height", 0);
            int refreshRate = v.second.get<int>("RefreshRate", 0);

            NvDisplayMode mode;
            mode.width = width;
            mode.height = height;
            mode.refreshRate = refreshRate;

            modes.push_back(mode);
        }
    }

    return modes;
}

std::vector<NvApp>
NvHTTP::getAppList()
{
    std::string appxml = openConnectionToString(m_BaseUrlHttps,
                                                 "applist",
                                                 "",
                                                 REQUEST_TIMEOUT_MS);

    verifyResponseStatus(appxml);

    std::vector<NvApp> apps;

    std::stringstream ss(appxml);
    boost::property_tree::ptree pt;

    try {
        read_xml(ss, pt);
    }
    catch (const boost::property_tree::xml_parser_error& e) {
        LOG(WARNING) << "XML parsing error: " << e.what();
        return apps;
    }
    catch (const std::exception& e) {
        LOG(WARNING) << "An error occurred: " << e.what();
        return apps;
    }

    std::regex hexColorRegex("^#[0-9a-fA-F]{6}$");
    // 遍历 <App> 节点
    BOOST_FOREACH(const boost::property_tree::ptree::value_type & v, pt.get_child("root"))
    {
        if (v.first == "App") {
            if (!apps.empty() && !apps.back().isInitialized()) {
                LOG(WARNING) << "Invalid applist XML";
                assert(false);
                return std::vector<NvApp>();
            }

            std::string appTitle = v.second.get<std::string>("AppTitle", "");
            int id = v.second.get<int>("ID", 0);
            std::string guid = v.second.get<std::string>("GUID", "");
            std::string isHdrSupported = v.second.get<std::string>("IsHdrSupported", "");
            std::string customImagePath = v.second.get<std::string>("CustomImagePath", "");
            std::string isAppCollectorGame = v.second.get<std::string>("IsAppCollectorGame", "");
            std::string desktopWallpaperColor = v.second.get<std::string>("DesktopWallpaperColor", "");
            std::string gamePlatform = v.second.get<std::string>("GamePlatform", "");
            NvApp app;
            app.name = appTitle;
            // Desktop always sets customImagePath to empty unless desktopWallpaperColor has a color value.
            if ("Desktop" == appTitle)
            {
                customImagePath = "";
                if (std::regex_match(desktopWallpaperColor, hexColorRegex))
                    customImagePath = "RGB: " + desktopWallpaperColor;
            }
            app.boxArt = customImagePath;
            app.gamePlatform = gamePlatform;
            app.id = id;
            app.guid = guid;
            app.hdrSupported = isHdrSupported == "1";
            app.isAppCollectorGame = isAppCollectorGame == "1";

            apps.push_back(app);
        }
    }

    return apps;
}

void NvHTTP::verifyResponseStatus(std::string xml)
{
    std::istringstream xmlStream(xml);
    boost::property_tree::ptree tree;

    // Parse the XML from the string stream
    try {
        boost::property_tree::read_xml(xmlStream, tree);
    }
    catch (const boost::property_tree::xml_parser_error& ) {
        throw GfeHttpResponseException(-1, "Malformed XML (missing root element)");
    }

    // Check if the root element exists
    auto root = tree.get_child_optional("root");
    if (!root) {
        throw GfeHttpResponseException(-1, "Malformed XML (missing root element)");
    }

    // Get status_code and status_message attributes
    unsigned int statusCode = root->get("<xmlattr>.status_code", 0u);
    std::string statusMessage = root->get("<xmlattr>.status_message", "");

    if (statusCode == 200)
    {
        // Successful
        return;
    }
    else
    {
        if (statusCode != 401)
        {
            //std::cerr << "Request failed: " << statusCode << " " << statusMessage << std::endl;
        }
        if (statusCode == static_cast<unsigned int>(-1) && statusMessage == "Invalid") 
        {
            // Special case handling an audio capture error
            statusCode = 418;
            statusMessage = "Missing audio capture device. Reinstalling RemotePlayHost should resolve this error.";
        }
        throw GfeHttpResponseException(static_cast<int>(statusCode), statusMessage);
    }
}


std::string
NvHTTP::getBoxArt(int appId)
{
    std::string image = openConnectionToString(m_BaseUrlHttps,
                                          "appasset",
                                          "appid="+ std::to_string(appId) +
                                          "&AssetType=2&AssetIdx=0",
                                          REQUEST_TIMEOUT_MS);
    return image;
}

std::string
NvHTTP::getXmlStringFromHex(std::string xml,
                             std::string tagName)
{
    std::string str = getXmlString(xml, tagName);
    if (str.empty())
        return str;

    std::string byteArray;
    boost::algorithm::unhex(str.begin(), str.end(), std::back_inserter(byteArray));
    return byteArray;
}

std::string
NvHTTP::getXmlString(std::string xml,
                      std::string tagName)
{
    std::stringstream ss(xml);
    boost::property_tree::ptree pt;

    read_xml(ss, pt);

    // 遍历整个 property tree，并获取指定名称节点的值
    for (auto& node : pt.get_child("root")) {
        if (node.first == tagName) {
            return node.second.data();
        }
    }

    return "";
}

std::string
NvHTTP::openConnectionToString(RazerUrl baseUrl,
                               std::string command,
                               std::string arguments,
                               int timeoutMs)
{
    std::string reply = openConnection(baseUrl, command, arguments, timeoutMs);

    return reply;
}

void NvHTTP::stopConnection()
{
    m_stop.store(true);
}

std::string NvHTTP::openConnection(RazerUrl baseUrl,
    std::string command,
    std::string arguments,
    int timeoutMs) {
    // Port must be set
    assert(baseUrl.port() != 0);

    // Build a URL for the request
    RazerUrl url(baseUrl);
    url.setPath("/" + command);
    url.setQuery("uniqueid=0123456789ABCDEF&uuid=" +
        Uuid::createUuid() +
        (arguments.empty() ? "" : ("&" + arguments)));

    const std::string IPPort = url.getIPAndPort();
    const std::string path = url.getPath();
    const int timeoutSec = timeoutMs / 1000;

    std::atomic<bool> requestFinished(false);

    SimpleWeb::error_code errorCode;
    std::string content;

    auto performRequest = [&](auto& client) {
        client.config.timeout_connect = timeoutSec;
        client.config.timeout = timeoutSec;

        if constexpr (std::is_same_v<std::decay_t<decltype(client)>, HttpsClient>) {
            if (!client.set_certificate_from_string(IdentityManager::get()->getCertificate(),
                IdentityManager::get()->getPrivateKey())) {
                throw NetworkReplyException(444, "Failed to set certificate or private key.");
            }
        }

        client.request("GET", path, "", [&content, &requestFinished, &errorCode](std::shared_ptr<typename std::decay_t<decltype(client)>::Response> response, const SimpleWeb::error_code& ec) {
            if (!ec)
                content = response->content.string();
            else
                errorCode = ec;
            requestFinished = true;
            });

        while (!requestFinished) {
            client.io_service->poll();
            if (ComputerManager::getInstance()->isExiting() || m_stop)
            {
                client.io_service->stop();
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (errorCode) {
            throw NetworkReplyException(errorCode.value(), errorCode.message());
        }
        };

    try {
        if (WMUtils::startsWith(url.getScheme(), "https")) {
            HttpsClient client(IPPort, false);
            performRequest(client);
        }
        else {
            HttpClient client(IPPort);
            performRequest(client);
        }
    }
    catch (...) {
        requestFinished = true;
        throw;
    }

    return content;
}
