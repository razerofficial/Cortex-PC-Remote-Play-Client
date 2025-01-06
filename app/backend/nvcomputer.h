#pragma once

#include "nvhttp.h"
#include "nvaddress.h"

#include <shared_mutex>


class CopySafeReadWriteLock : public std::shared_mutex {
public:
    CopySafeReadWriteLock() = default;

    // Don't actually copy the std::shared_mutex
    CopySafeReadWriteLock(const CopySafeReadWriteLock&) : std::shared_mutex() {}
    CopySafeReadWriteLock& operator=(const CopySafeReadWriteLock &) { return *this; }
};

class NvComputer
{
    friend class PcMonitorThreadRazer;
    friend class ComputerManager;
    friend class PendingQuitTaskRazer;
    friend class Stream;

private:
    void sortAppList();

    bool updateApp(const NvApp& newApp);

    bool updateAppList(std::vector<NvApp> newAppList);

    bool pendingQuit;

public:
    NvComputer() = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer(const NvComputer&) = default;

    // Caller is responsible for synchronizing read access to the other host
    NvComputer& operator=(const NvComputer &) = default;

    explicit NvComputer(NvHTTP& http, std::string serverInfo);

    explicit NvComputer(const boost::property_tree::ptree& settings, int computerIndex);

    void
    setRemoteAddress(std::string);

    bool
    update(const NvComputer& that);

    bool
    wake() const;

    enum ReachabilityType
    {
        RI_UNKNOWN,
        RI_LAN,
        RI_VPN,
    };

    ReachabilityType
    getActiveAddressReachability() const;

    std::vector<NvAddress>
    uniqueAddresses() const;

    void
    serialize(boost::property_tree::ptree& settings, int computerIndex, bool serializeApps) const;

    // Caller is responsible for synchronizing read access to both hosts
    bool
    isEqualSerialized(const NvComputer& that) const;

    enum PairState
    {
        PS_UNKNOWN,
        PS_PAIRED,
        PS_NOT_PAIRED
    };

    enum ComputerState
    {
        CS_UNKNOWN,
        CS_ONLINE,
        CS_OFFLINE
    };

    enum RazerIDPairMode
    {
        RP_UNKNOWN,
        RP_MANUAL,
        RP_AUTOMATIC,
        RP_DISABLE
    };

    // Ephemeral traits
    ComputerState state;
    PairState pairState;
    NvAddress activeAddress;
    uint16_t activeHttpsPort;
    int currentGameId;
    std::string gfeVersion;
    std::string appVersion;
    std::vector<NvDisplayMode> displayModes;
    int maxLumaPixelsHEVC;
    int serverCodecModeSupport;
    std::string gpuModel;
    bool isSupportedServerVersion;
    bool useSameRazerID;
    RazerIDPairMode razerPairMode;

    // Persisted traits
    NvAddress localAddress;
    NvAddress remoteAddress;
    NvAddress ipv6Address;
    NvAddress manualAddress;
    std::string macAddress;
    std::string name;
    bool hasCustomName;
    std::string uuid;
    std::string serverCert;
    std::vector<NvApp> appList;
    bool isNvidiaServerSoftware;
    // Remember to update isEqualSerialized() when adding fields here!

    // Synchronization
    mutable CopySafeReadWriteLock lock;

private:
    uint16_t externalPort;
};
