#include "nvcomputer.h"
#include "nvapp.h"
#include "settings/compatfetcher.h"
#include "boost/algorithm/string.hpp"
#include "utils.h"
#include "glog/logging.h"

#include <set>

#include <winsock2.h>
#include <iphlpapi.h>
#include <boost/asio.hpp>

#define SER_NAME "hostname"
#define SER_UUID "uuid"
#define SER_MAC "mac"
#define SER_LOCALADDR "localaddress"
#define SER_LOCALPORT "localport"
#define SER_REMOTEADDR "remoteaddress"
#define SER_REMOTEPORT "remoteport"
#define SER_MANUALADDR "manualaddress"
#define SER_MANUALPORT "manualport"
#define SER_IPV6ADDR "ipv6address"
#define SER_IPV6PORT "ipv6port"
#define SER_APPLIST "apps"
#define SER_SRVCERT "srvcert"
#define SER_CUSTOMNAME "customname"
#define SER_NVIDIASOFTWARE "nvidiasw"

NvComputer::NvComputer(const boost::property_tree::ptree& settings, int computerIndex)
{
    std::string computerPrex = std::to_string(computerIndex) + '\\';
    this->name = settings.get<std::string>(computerPrex + SER_NAME);
    this->uuid = settings.get<std::string>(computerPrex + SER_UUID);
    this->hasCustomName = settings.get<bool>(computerPrex + SER_CUSTOMNAME);
    this->macAddress = settings.get<std::string>(computerPrex + SER_MAC);
    this->localAddress = NvAddress(settings.get<std::string>(computerPrex + SER_LOCALADDR),
                                   settings.get<unsigned short>(computerPrex + SER_LOCALPORT, DEFAULT_HTTP_PORT));
    this->remoteAddress = NvAddress(settings.get<std::string>(computerPrex + SER_REMOTEADDR),
                                    settings.get<unsigned short>(computerPrex + SER_REMOTEPORT, DEFAULT_HTTP_PORT));
    this->ipv6Address = NvAddress(settings.get<std::string>(computerPrex + SER_IPV6ADDR),
                                  settings.get<unsigned short>(computerPrex + SER_IPV6PORT, DEFAULT_HTTP_PORT));
    this->manualAddress = NvAddress(settings.get<std::string>(computerPrex + SER_MANUALADDR),
                                    settings.get<unsigned short>(computerPrex + SER_MANUALPORT, DEFAULT_HTTP_PORT));
    this->serverCert = settings.get<std::string>(computerPrex + SER_SRVCERT);
    this->serverCert = StringUtils::replacePlaceholder(this->serverCert, "$CR$", "\n");
    this->isNvidiaServerSoftware = settings.get<bool>(computerPrex + SER_NVIDIASOFTWARE);

    int appCount = settings.get<int>(computerPrex + SER_APPLIST + "\\size", 0);
    this->appList.reserve(appCount);
    for (int appIndex = 1; appIndex <= appCount; appIndex++)
    {
        NvApp app(settings, computerIndex, appIndex);
        this->appList.push_back(app);
    }

    sortAppList();

    this->currentGameId = 0;
    this->pairState = PS_UNKNOWN;
    this->state = CS_UNKNOWN;
    this->gfeVersion = "";
    this->appVersion = "";
    this->maxLumaPixelsHEVC = 0;
    this->serverCodecModeSupport = 0;
    this->pendingQuit = false;
    this->gpuModel = "";
    this->isSupportedServerVersion = true;
    this->externalPort = this->remoteAddress.port();
    this->activeHttpsPort = 0;
    this->useSameRazerID = false;
    this->razerPairMode = RP_UNKNOWN;
}

void NvComputer::setRemoteAddress(std::string address)
{
    std::unique_lock<std::shared_mutex> locker(this->lock);

    assert(this->externalPort != 0);

    this->remoteAddress = NvAddress(address, this->externalPort);
}

void NvComputer::serialize(boost::property_tree::ptree& settings, int computerIndex, bool serializeApps) const
{
    std::string serverCertConvert = StringUtils::replacePlaceholder(serverCert, "\n", "$CR$");

    std::shared_lock<std::shared_mutex> locker(this->lock);

    std::string computerPrex = std::to_string(computerIndex) + '\\';
    settings.put(computerPrex + SER_NAME, name);
    settings.put(computerPrex + SER_CUSTOMNAME, hasCustomName);
    settings.put(computerPrex + SER_UUID, uuid);
    settings.put(computerPrex + SER_MAC, macAddress);
    settings.put(computerPrex + SER_LOCALADDR, localAddress.address());
    settings.put(computerPrex + SER_LOCALPORT, localAddress.port());
    settings.put(computerPrex + SER_REMOTEADDR, remoteAddress.address());
    settings.put(computerPrex + SER_REMOTEPORT, remoteAddress.port());
    settings.put(computerPrex + SER_IPV6ADDR, ipv6Address.address());
    settings.put(computerPrex + SER_IPV6PORT, ipv6Address.port());
    settings.put(computerPrex + SER_MANUALADDR, manualAddress.address());
    settings.put(computerPrex + SER_MANUALPORT, manualAddress.port());
    settings.put(computerPrex + SER_SRVCERT, serverCertConvert);
    settings.put(computerPrex + SER_NVIDIASOFTWARE, isNvidiaServerSoftware);

    if (!appList.empty() && serializeApps)
    {
        for(int appIndex = 0; appIndex < appList.size(); appIndex++)
        {
            appList[appIndex].serialize(settings, computerIndex, appIndex+1);
        }
        std::string prex = std::to_string(computerIndex) + '\\' + SER_APPLIST + '\\';
        settings.put(prex + "size", appList.size());
    }
}

bool NvComputer::isEqualSerialized(const NvComputer &that) const
{
    return this->name == that.name &&
           this->hasCustomName == that.hasCustomName &&
           this->uuid == that.uuid &&
           this->macAddress == that.macAddress &&
           this->localAddress == that.localAddress &&
           this->remoteAddress == that.remoteAddress &&
           this->ipv6Address == that.ipv6Address &&
           this->manualAddress == that.manualAddress &&
           this->serverCert == that.serverCert &&
           this->isNvidiaServerSoftware == that.isNvidiaServerSoftware &&
           this->appList == that.appList;
}

void NvComputer::sortAppList()
{
    std::stable_sort(appList.begin(), appList.end(), [](const NvApp& app1, const NvApp& app2) {
        // Step 1: "Desktop" application should be first
        if (app1.name == "Desktop") return true;
        if (app2.name == "Desktop") return false;

        // Step 2: Sort by lastAppStartTime in descending order
        if (app1.lastAppStartTime != app2.lastAppStartTime) {
            return app1.lastAppStartTime > app2.lastAppStartTime;
        }

        // Step 3: For applications with lastAppStartTime == 0, sort by name in ascending order
        if (app1.lastAppStartTime == 0 && app2.lastAppStartTime == 0) {
            std::string appName1 = app1.name;
            std::string appName2 = app2.name;

            boost::algorithm::to_lower(appName1);
            boost::algorithm::to_lower(appName2);

            return appName1 < appName2;
        }

        // If all else fails, maintain the current order
        return false;
        });
}

NvComputer::NvComputer(NvHTTP& http, std::string serverInfo)
{
    this->serverCert = http.serverCert();

    this->hasCustomName = false;
    this->name = NvHTTP::getXmlString(serverInfo, "hostname");
    if (this->name.empty()) {
        this->name = "UNKNOWN";
    }

    this->uuid = NvHTTP::getXmlString(serverInfo, "uniqueid");
    std::string newMacString = NvHTTP::getXmlString(serverInfo, "mac");
    if (newMacString != "00:00:00:00:00:00") {
        std::istringstream iss(newMacString);
        std::string segment;
        while (getline(iss, segment, ':')) {
            std::stringstream hexStream;
            hexStream << std::hex << std::setw(2) << std::setfill('0') << std::stoul(segment, nullptr, 16);
            macAddress += hexStream.str();
        }
    }

    std::string codecSupport = NvHTTP::getXmlString(serverInfo, "ServerCodecModeSupport");
    if (!codecSupport.empty()) {
        try
        {
            this->serverCodecModeSupport = std::stoi(codecSupport);
        }
        catch (...)
        {
            this->serverCodecModeSupport = SCM_H264;
            LOG(WARNING) << "ServerCodecModeSupport: std::stoi exception!";
        }
    }
    else {
        // Assume H.264 is always supported
        this->serverCodecModeSupport = SCM_H264;
    }

    std::string maxLumaPixelsHEVC = NvHTTP::getXmlString(serverInfo, "MaxLumaPixelsHEVC");
    if (!maxLumaPixelsHEVC.empty()) {
        try
        {
            this->maxLumaPixelsHEVC = std::stoi(maxLumaPixelsHEVC);
        }
        catch (...)
        {
            this->maxLumaPixelsHEVC = 0;
            LOG(WARNING) << "MaxLumaPixelsHEVC:std::stoi exception!";
        }
    }
    else {
        this->maxLumaPixelsHEVC = 0;
    }

    this->displayModes = NvHTTP::getDisplayModeList(serverInfo);
    std::stable_sort(this->displayModes.begin(), this->displayModes.end(),
                     [](const NvDisplayMode& mode1, const NvDisplayMode& mode2) {
        return (uint64_t)mode1.width * mode1.height * mode1.refreshRate <
                (uint64_t)mode2.width * mode2.height * mode2.refreshRate;
    });

    // We can get an IPv4 loopback address if we're using the GS IPv6 Forwarder
    this->localAddress = NvAddress(NvHTTP::getXmlString(serverInfo, "LocalIP"), http.httpPort());

    if (WMUtils::startsWith(this->localAddress.address(), "127.")){
        this->localAddress = NvAddress();
    }

    std::string httpsPort = NvHTTP::getXmlString(serverInfo, "HttpsPort");
    int iHttpsPort;
    try
    {
        iHttpsPort = std::stoi(httpsPort);
    }
    catch (...)
    {
        iHttpsPort = 0;
        LOG(WARNING) << "HttpsPort: std::stoi exception!";
    }
    if (httpsPort.empty() || (this->activeHttpsPort = iHttpsPort) == 0) {
        this->activeHttpsPort = DEFAULT_HTTPS_PORT;
    }

    // This is an extension which is not present in GFE. It is present for Sunshine to be able
    // to support dynamic HTTP WAN ports without requiring the user to manually enter the port.
    std::string remotePortStr = NvHTTP::getXmlString(serverInfo, "ExternalPort");
    int remotePort;
    try
    {
        remotePort = std::stoi(remotePortStr);
    }
    catch (...)
    {
        remotePort = 0;
        LOG(WARNING) << "ExternalPort: std::stoi exception!";
    }
    if (remotePortStr.empty() || (this->externalPort = remotePort) == 0) {
        this->externalPort = http.httpPort();
    }

    std::string remoteAddress = NvHTTP::getXmlString(serverInfo, "ExternalIP");
    if (!remoteAddress.empty()) {
        this->remoteAddress = NvAddress(remoteAddress, this->externalPort);
    }
    else {
        this->remoteAddress = NvAddress();
    }

    this->useSameRazerID = NvHTTP::getXmlString(serverInfo, "RazerIdIdentifier") == "true" ? true : false;
    std::string razerPairMode = NvHTTP::getXmlString(serverInfo, "RazerIdPairStatus");
    if (razerPairMode == "Manual")
        this->razerPairMode = RP_MANUAL;
    else if (razerPairMode == "Automatic")
        this->razerPairMode = RP_AUTOMATIC;
    else if (razerPairMode == "Disable")
        this->razerPairMode = RP_DISABLE;
    else
        this->razerPairMode = RP_UNKNOWN;

    // Real Nvidia host software (GeForce Experience and RTX Experience) both use the 'Mjolnir'
    // codename in the state field and no version of Sunshine does. We can use this to bypass
    // some assumptions about Nvidia hardware that don't apply to Sunshine hosts.
    this->isNvidiaServerSoftware = WMUtils::containsSubstring(NvHTTP::getXmlString(serverInfo, "state"), "MJOLNIR");

    this->pairState = NvHTTP::getXmlString(serverInfo, "PairStatus") == "1" ?
                PS_PAIRED : PS_NOT_PAIRED;
    this->currentGameId = NvHTTP::getCurrentGame(serverInfo);
    this->appVersion = NvHTTP::getXmlString(serverInfo, "appversion");
    this->gfeVersion = NvHTTP::getXmlString(serverInfo, "GfeVersion");
    this->gpuModel = NvHTTP::getXmlString(serverInfo, "gputype");
    this->activeAddress = http.address();
    this->state = NvComputer::CS_ONLINE;
    this->pendingQuit = false;
    this->isSupportedServerVersion = true;
}

bool NvComputer::wake() const 
{
    std::vector<uint8_t> wolPayload;

    {
        std::shared_lock<std::shared_mutex> locker(this->lock);

        if (state == NvComputer::CS_ONLINE) {
            LOG(WARNING) << name << " is already online" << std::endl;
            return true;
        }

        if (macAddress.empty()) {
            LOG(WARNING) << name << " has no MAC address stored" << std::endl;
            return false;
        }

        std::vector<uint8_t> macBytes;
        for (size_t i = 0; i < macAddress.length(); i += 2) {
            std::string byteString = macAddress.substr(i, 2);
            uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
            macBytes.push_back(byte);
        }

        // Create the WoL payload
        wolPayload.insert(wolPayload.end(), { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF });
        for (int i = 0; i < 16; ++i) {
            wolPayload.insert(wolPayload.end(), macBytes.begin(), macBytes.end());
        }
        assert(wolPayload.size() == 102);
    }

    // Ports used as-is
    const uint16_t STATIC_WOL_PORTS[] = {
        9,  // Standard WOL port (privileged port)
        47009,  // Port opened by Moonlight Internet Hosting Tool for WoL (non-privileged port)
    };

    // Ports offset by the HTTP base port for hosts using alternate ports
    const uint16_t DYNAMIC_WOL_PORTS[] = {
        47998, 47999, 48000, 48002, 48010,  // Ports opened by GFE
    };

    // Add the addresses that we know this host to be
    // and broadcast addresses for this link just in
    // case the host has timed out in ARP entries.
    std::unordered_map<std::string, uint16_t> addressMap;
    std::set<uint16_t> basePortSet;
    for (const NvAddress& addr : uniqueAddresses()) {
        addressMap[addr.address()] = addr.port();
        basePortSet.insert(addr.port());
    }
    addressMap["255.255.255.255"] = 0;

    // Try to broadcast on all available NICs
    ULONG outBufLen = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, NULL, NULL, &outBufLen) != ERROR_BUFFER_OVERFLOW) {
        LOG(ERROR) << "GetAdaptersAddresses failed with error: " << GetLastError() << std::endl;
        return false;
    }

    std::unique_ptr<IP_ADAPTER_ADDRESSES, void(*)(void*)> pAddresses(
        (IP_ADAPTER_ADDRESSES*)malloc(outBufLen), free);
    if (pAddresses == nullptr) {
        LOG(ERROR) << "Memory allocation failed" << std::endl;
        return false;
    }

    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, NULL, pAddresses.get(), &outBufLen) != ERROR_SUCCESS) {
        LOG(ERROR) << "GetAdaptersAddresses failed with error: " << GetLastError() << std::endl;
        return false;
    }

    for (auto pCurrAddresses = pAddresses.get(); pCurrAddresses != nullptr; pCurrAddresses = pCurrAddresses->Next) {
        // Ensure the interface is up and skip the loopback adapter
        if ((pCurrAddresses->OperStatus != IfOperStatusUp) || (pCurrAddresses->IfType == IF_TYPE_SOFTWARE_LOOPBACK)) {
            continue;
        }

        // Store the broadcast address for this NIC
        if (pCurrAddresses->FirstUnicastAddress) {
            for (auto pUnicastAddress = pCurrAddresses->FirstUnicastAddress; pUnicastAddress != nullptr; pUnicastAddress = pUnicastAddress->Next) {
                if (pUnicastAddress->Address.lpSockaddr->sa_family == AF_INET) {
                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &((struct sockaddr_in*)pUnicastAddress->Address.lpSockaddr)->sin_addr, ipStr, INET_ADDRSTRLEN);
                    addressMap[ipStr] = 0;
                }
            }
        }
    }

    // Try all unique address strings or host names
    bool success = false;
    boost::asio::io_context ioContext;
    for (auto& [addressStr, port] : addressMap) {
        boost::system::error_code ec;
        boost::asio::ip::address_v4 address = boost::asio::ip::address_v4::from_string(addressStr, ec);
        if (ec) {
            LOG(ERROR) << "Error resolving " << addressStr << ": " << ec.message() << std::endl;
            continue;
        }

        boost::asio::ip::udp::socket sock(ioContext);
        sock.open(boost::asio::ip::udp::v4());

        // Send to all static ports
        for (uint16_t p : STATIC_WOL_PORTS) {
            sock.send_to(boost::asio::buffer(wolPayload), boost::asio::ip::udp::endpoint(address, p));
            LOG(INFO) << "Sent WoL packet to " << name << " via " << address.to_string() << ":" << p << std::endl;
            success = true;
        }

        std::vector<uint16_t> basePorts;
        if (port != 0) {
            // If we have a known base port for this address, use only that port
            basePorts.push_back(port);
        }
        else {
            // If this is a broadcast address without a known HTTP port, try all of them
            basePorts.insert(basePorts.end(), basePortSet.begin(), basePortSet.end());
        }

        // Send to all dynamic ports using the HTTP port offset(s) for this address
        for (uint16_t basePort : basePorts) {
            for (uint16_t p : DYNAMIC_WOL_PORTS) {
                p = (p - 47989) + basePort;
                sock.send_to(boost::asio::buffer(wolPayload), boost::asio::ip::udp::endpoint(address, p));
                LOG(INFO) << "Sent WoL packet to " << name << " via " << address.to_string() << ":" << p << std::endl;
                success = true;
            }
        }
    }

    return success;
}

NvComputer::ReachabilityType NvComputer::getActiveAddressReachability() const
{
    NvAddress copyOfActiveAddress;

    {
        std::shared_lock<std::shared_mutex> locker(this->lock);

        if (activeAddress.isNull()) {
            return ReachabilityType::RI_UNKNOWN;
        }

        copyOfActiveAddress = activeAddress;
    }

    try
    {
        boost::asio::io_context ioContext;
        boost::asio::ip::tcp::socket socket(ioContext);
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(copyOfActiveAddress.address()), copyOfActiveAddress.port());
        socket.connect(endpoint);

        boost::asio::ip::address local_address = socket.local_endpoint().address();

#if defined(_WIN32)
        ULONG outBufLen = 0;
        GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, NULL, NULL, &outBufLen);
        IP_ADAPTER_ADDRESSES* pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
        if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST, NULL, pAddresses, &outBufLen) == ERROR_SUCCESS) {
            for (auto pCurrAddresses = pAddresses; pCurrAddresses != nullptr; pCurrAddresses = pCurrAddresses->Next) {
                for (auto pUnicastAddress = pCurrAddresses->FirstUnicastAddress; pUnicastAddress != nullptr; pUnicastAddress = pUnicastAddress->Next) {
                    if (pUnicastAddress->Address.lpSockaddr->sa_family == AF_INET) {
                        char ipStr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &((struct sockaddr_in*)pUnicastAddress->Address.lpSockaddr)->sin_addr, ipStr, INET_ADDRSTRLEN);
                        if (local_address.to_string() == std::string(ipStr)) {
                            free(pAddresses);
                            return ReachabilityType::RI_LAN;
                        }
                    }
                }
            }
        }
        free(pAddresses);
#else
        struct ifaddrs* ifaddr, * ifa;
        if (getifaddrs(&ifaddr) == -1) {
            perror("getifaddrs");
            return ReachabilityType::RI_VPN;
        }
        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
                auto addr = boost::asio::ip::address_v4::from_string(inet_ntoa(((struct sockaddr_in*)ifa->ifa_addr)->sin_addr));
                if (addr == local_address) {
                    freeifaddrs(ifaddr);
                    return ReachabilityType::RI_LAN;
                }
            }
        }
        freeifaddrs(ifaddr);
#endif
        return ReachabilityType::RI_VPN;
    }
    catch (const boost::system::system_error& e)
    {
        return ReachabilityType::RI_UNKNOWN;
    }
}

bool NvComputer::updateApp(const NvApp& newApp)
{
    for (NvApp& existingApp : appList) 
    {
        if (existingApp.id == newApp.id)
        {
            existingApp = newApp;
            sortAppList();
            return true;
        }
    }

    return false;
}

bool NvComputer::updateAppList(std::vector<NvApp> newAppList) {
    if (appList == newAppList) {
        return false;
    }

    // Propagate client-side attributes to the new app list
    for (const NvApp& existingApp : appList) {
        for (NvApp& newApp : newAppList) {
            if (existingApp.id == newApp.id) {
                newApp.hidden = existingApp.hidden;
                newApp.directLaunch = existingApp.directLaunch;
                newApp.lastAppStartTime = existingApp.lastAppStartTime;
            }
        }
    }

    appList = newAppList;
    sortAppList();
    return true;
}

std::vector<NvAddress> NvComputer::uniqueAddresses() const
{
    std::shared_lock<std::shared_mutex> locker(this->lock);
    std::vector<NvAddress> uniqueAddressList;

    // Start with addresses correctly ordered
    uniqueAddressList.push_back(activeAddress);
    uniqueAddressList.push_back(localAddress);
    uniqueAddressList.push_back(remoteAddress);
    uniqueAddressList.push_back(ipv6Address);
    uniqueAddressList.push_back(manualAddress);

    // Prune duplicates (always giving precedence to the first)
    for (int i = 0; i < uniqueAddressList.size(); i++) {
        if (uniqueAddressList[i].isNull()) {
            uniqueAddressList.erase(uniqueAddressList.begin() + i);
            i--;
            continue;
        }
        for (int j = i + 1; j < uniqueAddressList.size(); j++) {
            if (uniqueAddressList[i] == uniqueAddressList[j]) {
                // Always remove the later occurrence
                uniqueAddressList.erase(uniqueAddressList.begin() + j);
                j--;
            }
        }
    }

    // We must have at least 1 address
    assert(!uniqueAddressList.empty());

    return uniqueAddressList;
}

bool NvComputer::update(const NvComputer& that)
{
    bool changed = false;

    // Lock us for write and them for read
    std::unique_lock<std::shared_mutex> wlocker(this->lock);
    std::shared_lock<std::shared_mutex> rlocker(that.lock);

    // UUID may not change or we're talking to a new PC
    assert(this->uuid == that.uuid);

#define ASSIGN_IF_CHANGED(field)       \
    if (this->field != that.field) {   \
        this->field = that.field;      \
        changed = true;                \
    }

#define ASSIGN_IF_CHANGED_AND_NONEMPTY(field) \
    if (!that.field.isEmpty() &&              \
        this->field != that.field) {          \
        this->field = that.field;             \
        changed = true;                       \
    }

#define ASSIGN_IF_CHANGED_AND_NONNULL(field)  \
    if (!that.field.isNull() &&               \
        this->field != that.field) {          \
        this->field = that.field;             \
        changed = true;                       \
    }

#define ASSIGN_IF_CHANGED_STRING_AND_NONNULL(field)  \
    if (!that.field.empty() &&                       \
            this->field != that.field) {             \
            this->field = that.field;                \
            changed = true;                          \
    }

#define ASSIGN_IF_CHANGED_VECTOR_AND_NONEMPTY(field) \
    if (!that.field.empty() &&                       \
            this->field != that.field) {             \
            this->field = that.field;                \
            changed = true;                          \
    }

    if (!hasCustomName) {
        // Only overwrite the name if it's not custom
        ASSIGN_IF_CHANGED(name);
    }
    ASSIGN_IF_CHANGED_STRING_AND_NONNULL(macAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(localAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(remoteAddress);
    ASSIGN_IF_CHANGED_AND_NONNULL(ipv6Address);
    ASSIGN_IF_CHANGED_AND_NONNULL(manualAddress);
    ASSIGN_IF_CHANGED(activeHttpsPort);
    ASSIGN_IF_CHANGED(externalPort);
    ASSIGN_IF_CHANGED(pairState);
    ASSIGN_IF_CHANGED(serverCodecModeSupport);
    ASSIGN_IF_CHANGED(currentGameId);
    ASSIGN_IF_CHANGED(activeAddress);
    ASSIGN_IF_CHANGED(state);
    ASSIGN_IF_CHANGED_STRING_AND_NONNULL(gfeVersion);
    ASSIGN_IF_CHANGED_STRING_AND_NONNULL(appVersion);
    ASSIGN_IF_CHANGED(isSupportedServerVersion);
    ASSIGN_IF_CHANGED(useSameRazerID);
    ASSIGN_IF_CHANGED(razerPairMode);
    ASSIGN_IF_CHANGED(isNvidiaServerSoftware);
    ASSIGN_IF_CHANGED(maxLumaPixelsHEVC);
    ASSIGN_IF_CHANGED_STRING_AND_NONNULL(gpuModel);
    ASSIGN_IF_CHANGED_STRING_AND_NONNULL(serverCert);
    ASSIGN_IF_CHANGED_VECTOR_AND_NONEMPTY(displayModes);

    if (!that.appList.empty()) {
        // updateAppList() handles merging client-side attributes
        updateAppList(that.appList);
    }

    return changed;
}
