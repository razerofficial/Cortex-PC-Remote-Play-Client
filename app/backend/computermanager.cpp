#include "mainwindow.h"
#include "computermanager.h"
#include "windowsmessage.h"
#include "utils.h"
#include "razer.h"
#include "streaming/session.h"
#include "settings/configuer.h"
#include "httpserver.h"

#include <Limelight.h>
#include <glog/logging.h>
#include <boost/algorithm/string.hpp>


#include <iomanip>
#include <random>
#include <tlhelp32.h>

#define SER_HOSTS "hosts"
#define SER_HOSTS_BACKUP "hostsbackup"

class PcMonitorThreadRazer : public PcMonitorThreadInterfaceRazer
{
#define TRIES_BEFORE_OFFLINING 2
#define POLLS_PER_APPLIST_FETCH 10

public:
    PcMonitorThreadRazer(NvComputer* computer, ComputerManager* computerManager)
        : PcMonitorThreadInterfaceRazer()
        , m_Computer(computer)
        , m_ComputerManager(computerManager)
    {

    }

    void start()
    {
        m_thread = std::thread(&PcMonitorThreadRazer::run, this);
    }

    void computerStateChanged(NvComputer* computer)
    {
        PcMonitorMessage* e = new PcMonitorMessage(computer, m_ComputerManager);
        extern MyWindow g_window;
        HWND hWnd = g_window.getHandle();
        PostMessage(hWnd, e->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(e));
    }

private:
    bool tryPollComputer(NvAddress address, bool& changed)
    {
        NvHTTP http(address, 0, m_Computer->serverCert);

        std::string serverInfo;
        try {
            serverInfo = http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true);
        } catch (...) {
            return false;
        }

        NvComputer newState(http, serverInfo);

        // Ensure the machine that responded is the one we intended to contact
        if (m_Computer->uuid != newState.uuid) {
            LOG(INFO) << "Found unexpected PC " << newState.name << " looking for " << m_Computer->name;
            return false;
        }

        changed = m_Computer->update(newState);
        return true;
    }

    bool updateAppList(bool& changed)
    {
        NvHTTP http(m_Computer);

        std::vector<NvApp> appList;

        try {
            appList = http.getAppList();
            if (appList.empty()) {
                return false;
            }
        } catch (...) {
            return false;
        }

        std::unique_lock<std::shared_mutex> wlocker(m_Computer->lock);
        changed = m_Computer->updateAppList(appList);
        return true;
    }

    void run()
    {
        // Always fetch the applist the first time
        int pollsSinceLastAppListFetch = POLLS_PER_APPLIST_FETCH;
        while (!isInterruptionRequested()) {
            bool stateChanged = false;
            bool online = false;
            bool wasOnline = m_Computer->state == NvComputer::CS_ONLINE;
            for (int i = 0; i < (wasOnline ? TRIES_BEFORE_OFFLINING : 1) && !online; i++) {
                for (auto& address : m_Computer->uniqueAddresses()) {
                    if (isInterruptionRequested()) {
                        return;
                    }

                    if (tryPollComputer(address, stateChanged)) {
                        if (!wasOnline) {
                            LOG(INFO) << m_Computer->name << " is now online at " << m_Computer->activeAddress.toString();
                        }
                        online = true;
                        break;
                    }
                }
            }

            // Check if we failed after all retry attempts
            // Note: we don't need to acquire the read lock here,
            // because we're on the writing thread.
            if (!online && m_Computer->state != NvComputer::CS_OFFLINE) {
                LOG(INFO) << m_Computer->name << "is now offline";
                m_Computer->state = NvComputer::CS_OFFLINE;
                stateChanged = true;
            }

            // Grab the applist if it's empty or it's been long enough that we need to refresh
            pollsSinceLastAppListFetch++;
            if (m_Computer->state == NvComputer::CS_ONLINE &&
                m_Computer->pairState == NvComputer::PS_PAIRED &&
                (m_Computer->appList.empty() || pollsSinceLastAppListFetch >= POLLS_PER_APPLIST_FETCH)) {
                // Notify prior to the app list poll since it may take a while, and we don't
                // want to delay onlining of a machine, especially if we already have a cached list.
                if (stateChanged) {
                    computerStateChanged(m_Computer);
                    stateChanged = false;
                }

                if (updateAppList(stateChanged)) {
                    pollsSinceLastAppListFetch = 0;
                }
            }

            if (stateChanged) {
                // Tell anyone listening that we've changed state
                computerStateChanged(m_Computer);
            }

            // Wait a bit to poll again, but do it in 100 ms chunks
            // so we can be interrupted reasonably quickly.
            // FIXME: QWaitCondition would be better.
            for (int i = 0; i < 30 && !isInterruptionRequested(); i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        m_isFinished = true;
    }

private:
    NvComputer* m_Computer;
    ComputerManager* m_ComputerManager;
};

class HeartBeat
{
public:
    HeartBeat(ComputerManager* computerManager)
        : m_ComputerManager(computerManager)
        , m_interruptionRequested(false)
    {
    }

    ~HeartBeat()
    {
        if (!isInterruptionRequested())
            requestInterruption();

        if (m_thread.joinable())
            m_thread.join();
    }

    void requestInterruption()
    {
        std::lock_guard<std::mutex> lock(m_interruptionMutex);
        m_interruptionRequested = true;
    }

    bool isInterruptionRequested()
    {
        std::lock_guard<std::mutex> lock(m_interruptionMutex);
        return m_interruptionRequested;
    }

    void run()
    {
        while (!isInterruptionRequested())
        {
            if (!isProcessRunning("RazerCortex.exe")) {
                LOG(INFO) << "Cortex is not running, exit client.";
                m_ComputerManager->exitMessageLoop();
                break;
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
    }

    void start()
    {
        m_thread = std::thread(&HeartBeat::run, this);
    }

    void wait()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    bool isProcessRunning(const std::string& processName)
    {
        HANDLE hProcessSnap;
        PROCESSENTRY32 pe32;
        hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (hProcessSnap == INVALID_HANDLE_VALUE) {
            LOG(ERROR) << "Failed to create process snapshot.";
            return false;
        }

        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (!Process32First(hProcessSnap, &pe32)) {
            CloseHandle(hProcessSnap);
            LOG(ERROR) << "Failed to retrieve process information.";
            return false;
        }

        do {
            char exeFile[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, pe32.szExeFile, -1, exeFile, MAX_PATH, nullptr, nullptr);

            if (processName == exeFile) {
                CloseHandle(hProcessSnap);
                return true;
            }
        } while (Process32Next(hProcessSnap, &pe32));

        CloseHandle(hProcessSnap);
        return false;
    }

private:
    ComputerManager* m_ComputerManager;
    std::mutex m_interruptionMutex;
    bool m_interruptionRequested;
    std::thread m_thread;
};

ComputerManager* ComputerManager::s_instance = nullptr;
std::once_flag ComputerManager::s_initFlag;

ComputerManager* ComputerManager::getInstance()
{
    std::call_once(s_initFlag, []() {
        s_instance = new ComputerManager(StreamingPreferences::get());
        });
    return s_instance;
}

void ComputerManager::destroyInstance() {
    delete s_instance;
    s_instance = nullptr;
}

ComputerManager::ComputerManager(StreamingPreferences* prefs)
    : m_Prefs(prefs),
      m_PollingRef(0),
      m_NeedsDelayedFlush(false)
{
    m_asynsTasks = new AsyncTaskManager();
    m_server = new HTTPServer();

    Configure* settings = Configure::getInstance();

    int hosts = settings->getHostsSize();
    boost::property_tree::ptree hostTree= settings->getHosts();

    for(int i=1; i<=hosts; i++)
    {
        NvComputer* computer = new NvComputer(hostTree, i);
        m_KnownHosts[computer->uuid] = computer;
        m_LastSerializedHosts[computer->uuid] = *computer;
    }

    // Start the delayed flush thread to handle saveHosts() calls
    m_DelayedFlushThread = new DelayedFlushThreadRazer(this);
    m_DelayedFlushThread->start();

    m_heartBeat = new HeartBeat(this);
    m_heartBeat->start();

    m_readyQuit = false;
}

ComputerManager::~ComputerManager()
{
    delete m_server;
    m_server = nullptr;

    m_asynsTasks->stopAsyncTask();
    delete m_asynsTasks;
    m_asynsTasks = nullptr;

    delete m_heartBeat;
    m_heartBeat = nullptr;

    // Stop the delayed flush thread before acquiring the lock in write mode
    // to avoid deadlocking with a flush that needs the lock in read mode.
    {
        // Wake the delayed flush thread
        m_DelayedFlushThread->requestInterruption();
        m_DelayedFlushCondition.notify_one();

        // Wait for it to terminate (and finish any pending flush)
        m_DelayedFlushThread->wait();
        delete m_DelayedFlushThread;

        // Delayed flushes should have completed by now
        assert(!m_NeedsDelayedFlush);
    }

    std::unique_lock<std::shared_mutex> locker(m_Lock);
    m_Mdns.reset();

    // Interrupt polling
    for (auto& pair : m_PollEntries) {
        pair.second->interrupt();
    }

    // Delete all polling entries (and associated threads)
    for (auto& pair : m_PollEntries) {
        delete pair.second;
    }

    // Destroy all NvComputer objects now that polling is halted
    for (auto& pair : m_KnownHosts) {
        delete pair.second;
    }
}

void DelayedFlushThreadRazer::run()
{
    m_isRunning = true;
    for (;;) {
        // Wait for a delayed flush request or an interruption
        {
            std::unique_lock<std::mutex> locker(m_ComputerManager->m_DelayedFlushMutex);

            while (!isInterruptionRequested() && !m_ComputerManager->m_NeedsDelayedFlush) {
                m_ComputerManager->m_DelayedFlushCondition.wait(locker);
            }

            // Bail without flushing if we woke up for an interruption alone.
            // If we have both an interruption and a flush request, do the flush.
            if (!m_ComputerManager->m_NeedsDelayedFlush) {
                assert(isInterruptionRequested());
                break;
            }

            // Reset the delayed flush flag to ensure any racing saveHosts() call will set it again
            m_ComputerManager->m_NeedsDelayedFlush = false;

            // Update the last serialized hosts map under the delayed flush mutex
            m_ComputerManager->m_LastSerializedHosts.clear();
            for (const auto& pair : m_ComputerManager->m_KnownHosts) {
                // Copy the current state of the NvComputer to allow us to check later if we need
                // to serialize it again when attribute updates occur.
                std::shared_lock<std::shared_mutex> computerLock(pair.second->lock);
                m_ComputerManager->m_LastSerializedHosts[pair.second->uuid] = *pair.second;
            }
        }

        // Perform the flush
        {
            Configure* settings = Configure::getInstance();

            // First, write to the backup location
            //settings.beginWriteArray(SER_HOSTS_BACKUP);
            // {
            //     //QReadLocker lock(&m_ComputerManager->m_Lock);
            //     boost::shared_lock<boost::shared_mutex> locker(m_ComputerManager->m_Lock);
            //     boost::property_tree::ptree hostTree;
            //     int i = 0;
            //     for (const auto& pair : m_ComputerManager->m_KnownHosts) {
            //         settings.setArrayIndex(i++);
            //         pair.second->serialize(hostTree, ++i, false);
            //     }
            //     settings->addChild(SER_HOSTS_BACKUP, hostTree);
            //     settings->setValue(Configure::HostsBackup, "size", i);
            // }
            //settings.endArray();

            // Next, write to the primary location
            {
                std::shared_lock<std::shared_mutex> locker(m_ComputerManager->m_Lock);
                boost::property_tree::ptree hostTree;
                int i = 0;
                for (const auto& pair : m_ComputerManager->m_KnownHosts) {
                    pair.second->serialize(hostTree, ++i, true);
                }
                settings->setHosts(hostTree);
                settings->setHostsSize(i);
            }

            // Finally, delete the backup copy
            //settings->remove(SER_HOSTS_BACKUP);

            settings->saveHosts();
        }
    }
    m_isRunning = false;
}

void ComputerManager::saveHosts()
{
    assert(m_DelayedFlushThread != nullptr && m_DelayedFlushThread->isRunning());

    // Punt to a worker thread because QSettings on macOS can take ages (> 500 ms)
    // to persist our host list to disk (especially when a host has a bunch of apps).
    std::lock_guard<std::mutex> locker(m_DelayedFlushMutex);
    m_NeedsDelayedFlush = true;
    m_DelayedFlushCondition.notify_one();
}

// QHostAddress ComputerManager::getBestGlobalAddressV6(std::vector<QHostAddress> &addresses)
// {
//     for (const QHostAddress& address : addresses) {
//         if (address.protocol() == QAbstractSocket::IPv6Protocol) {
//             if (address.isInSubnet(QHostAddress("fe80::"), 10)) {
//                 // Link-local
//                 continue;
//             }

//             if (address.isInSubnet(QHostAddress("fec0::"), 10)) {
//                 qInfo() << "Ignoring site-local address:" << address;
//                 continue;
//             }

//             if (address.isInSubnet(QHostAddress("fc00::"), 7)) {
//                 qInfo() << "Ignoring ULA:" << address;
//                 continue;
//             }

//             if (address.isInSubnet(QHostAddress("2002::"), 16)) {
//                 qInfo() << "Ignoring 6to4 address:" << address;
//                 continue;
//             }

//             if (address.isInSubnet(QHostAddress("2001::"), 32)) {
//                 qInfo() << "Ignoring Teredo address:" << address;
//                 continue;
//             }

//             return address;
//         }
//     }

//     return QHostAddress();
// }

void ComputerManager::startPolling()
{
    const int maxWaitTimeMs = 3000;
    int waitedMs = 0;
    const int checkIntervalMs = 50;

    // If razer id is empty, wait up to 3 seconds to see if it is updated
    while (Razer::getInstance()->getUUID().empty() && waitedMs < maxWaitTimeMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
        waitedMs += checkIntervalMs;
    }

    if (Razer::getInstance()->getUUID().empty())
        LOG(ERROR) << "Razer ID is empty.";

    std::unique_lock<std::shared_mutex> locker(m_Lock);

    if (++m_PollingRef > 1) {
        return;
    }

    if (m_Prefs->enableMdns) {
            m_Mdns.reset(new MDNSWarp("_rzstream._tcp.local."));
            std::function<void(mdns_cpp::mDNS::mdns_out)> callback = std::bind(&ComputerManager::handleMdnsServiceResolved, this, std::placeholders::_1);
            m_Mdns->resolvedHost(callback);
            m_Mdns->start();
    }
    else {
        LOG(WARNING) << "mDNS is disabled by user preference";
    }

    for (const auto& pair : m_KnownHosts) {
        startPollingComputer(pair.second);
    }
}

// Must hold m_Lock for write
void ComputerManager::startPollingComputer(NvComputer* computer)
{
    if (m_PollingRef == 0) {
        return;
    }

    ComputerPollingEntryRazer* pollingEntry;

    if (m_PollEntries.end() == m_PollEntries.find(computer->uuid)){
        pollingEntry = m_PollEntries[computer->uuid] = new ComputerPollingEntryRazer();
    }
    else {
        pollingEntry = m_PollEntries[computer->uuid];
    }

    if (!pollingEntry->isActive()) {
        PcMonitorThreadRazer* thread = new PcMonitorThreadRazer(computer, this);

        pollingEntry->setActiveThread(thread);
        thread->start();
    }
}

void ComputerManager::handleMdnsServiceResolved(mdns_cpp::mDNS::mdns_out mdnsOut)
{
    addNewHost(NvAddress(mdnsOut.ipv4, mdnsOut.port), true, NvAddress(mdnsOut.ipv6, mdnsOut.port));
}

void ComputerManager::saveHost(NvComputer *computer)
{
    // If no serializable properties changed, don't bother saving hosts
    std::unique_lock<std::mutex> delayLock(m_DelayedFlushMutex);
    std::shared_lock<std::shared_mutex> computerLock(computer->lock);

    if (m_LastSerializedHosts.find(computer->uuid) == m_LastSerializedHosts.end() ||
        !m_LastSerializedHosts[computer->uuid].isEqualSerialized(*computer)) {
        computerLock.unlock();
        delayLock.unlock();
        saveHosts();
    }
}

void ComputerManager::handleComputerStateChanged(NvComputer* computer)
{
    if (computer->pendingQuit && computer->currentGameId == 0) {
        computer->pendingQuit = false;
    }

    // Save updates to this host
    saveHost(computer);
}

std::vector<NvComputer*> ComputerManager::getComputers()
{
    std::shared_lock<std::shared_mutex> locker(m_Lock);

    // Return a sorted host list
    std::vector<NvComputer*> hosts;
    for (const auto& pair : m_KnownHosts) {
        hosts.push_back(pair.second);
    }
    std::stable_sort(hosts.begin(), hosts.end(), [](const NvComputer* host1, const NvComputer* host2) {
        std::string hostName1 = host1->name;
        std::string hostName2 = host2->name;

        boost::algorithm::to_lower(hostName1);
        boost::algorithm::to_lower(hostName2);
        return hostName1 < hostName2;
    });
    return hosts;
}

std::string ComputerManager::deleteHost(NvComputer* computer)
{
    if (!m_asynsTasks)
        return "";

    return m_asynsTasks->startupDeleteTask(computer);
}

bool ComputerManager::getDeleteTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    if (!m_asynsTasks)
        return false;

    return m_asynsTasks->getDeleteTaskResult(taskId, result);
}

void ComputerManager::streamHost(NvComputer* computer, const NvApp& app)
{
    StreamMessage* msg = new StreamMessage(computer, app);
    extern MyWindow g_window;
    HWND hWnd = g_window.getHandle();
    PostMessage(hWnd, msg->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(msg));
    return ;
}

bool ComputerManager::getStreamTaskResult(AsyncTaskManager::Result& result)
{
    Session::State state = Session::streamingState();

    auto setErrorResult = [&result](const std::string& errorString) {
        result.isCompleted = true;
        result.isSucceed = false;
        result.errorString = errorString;
    };
    
    switch (state)
    {
    case Session::NoError:
    {
        result.isCompleted = false;
        result.isSucceed = false;
        result.errorString.clear();
    }
    break;
    case Session::SessionInitErr:
        setErrorResult(Razer::textMap.at("Error initializing session."));
        break;
    case Session::ConnectionErr:
        setErrorResult(Razer::textMap.at("Error connecting to host."));
        break;
    case Session::SDLWinCreateErr:
        setErrorResult(Razer::textMap.at("Internal error."));
        break;
    case Session::ResetErr:
        setErrorResult(Razer::textMap.at("Error resetting decoder."));
        break;
    case Session::AbnormalExit:
        setErrorResult(Razer::textMap.at("Connection terminated."));
        break;
    case Session::NormalExit:
    {
        result.isCompleted = true;
        result.isSucceed = true;
        result.errorString.clear();
    }
    break;
    default:
        break;
    }

    return true;
}

void ComputerManager::exitMessageLoop()
{
    m_readyQuit = true;
    Session::Quit();
    QuitMessage* msg = new QuitMessage();
    extern MyWindow g_window;
    HWND hWnd = g_window.getHandle();
    PostMessage(hWnd, msg->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(msg));
}

bool ComputerManager::isExiting()
{
    return m_readyQuit;
}

void ComputerManager::renameHost(NvComputer* computer, std::string name)
{
    {
        std::unique_lock<std::shared_mutex> locker(computer->lock);

        computer->name = name;
        computer->hasCustomName = true;
    }

    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

void ComputerManager::clientSideAttributeUpdated(NvComputer* computer)
{
    // Notify the UI of the state change
    handleComputerStateChanged(computer);
}

std::string ComputerManager::pairHost(NvComputer* computer, std::string pin, bool useRazerJWT)
{
    if (!m_asynsTasks)
        return "";

    return m_asynsTasks->startupPairTask(computer, pin, useRazerJWT);
}

bool ComputerManager::getPairTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    if (!m_asynsTasks)
        return false;

    return m_asynsTasks->getPairTaskResult(taskId, result);
}

bool ComputerManager::cancelPairTask(const std::string& taskId, AsyncTaskManager::Result& result)
{
    if (!m_asynsTasks)
        return false;

    return m_asynsTasks->cancelPairTask(taskId, result);
}

std::string ComputerManager::quitRunningApp(NvComputer* computer)
{
    if (!m_asynsTasks)
        return "";

    {
        std::unique_lock<std::shared_mutex> locker(computer->lock);
        computer->pendingQuit = true;
    }

    return m_asynsTasks->startupAPPQuitTask(computer);
}

bool ComputerManager::getQuitTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    if (!m_asynsTasks)
        return false;

    return m_asynsTasks->getAPPQuitTaskResult(taskId, result);
}

void ComputerManager::stopPollingAsync()
{
    std::unique_lock<std::shared_mutex> locker(m_Lock);

    assert(m_PollingRef > 0);
    if (--m_PollingRef > 0) {
        return;
    }

    m_Mdns.reset();

    // Interrupt all threads, but don't wait for them to terminate
    for (auto& pair : m_PollEntries) {
        pair.second->interrupt();
    }
}

std::string ComputerManager::addNewHostManually(std::string address)
{
    return addNewHost(NvAddress(address, DEFAULT_HTTP_PORT), false);
}

std::string ComputerManager::addNewHost(NvAddress address, bool mdns, NvAddress mdnsIpv6Address)
{
    if (!m_asynsTasks)
        return "";

    return m_asynsTasks->startupAddTask(address, mdnsIpv6Address, mdns);
}

bool ComputerManager::getAddTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    if (!m_asynsTasks)
        return false;

    return m_asynsTasks->getAddTaskResult(taskId, result);
}

std::string ComputerManager::generatePinString()
{
    std::uniform_int_distribution<int> dist(0, 9999);
    std::random_device rd;
    std::mt19937 engine(rd());


    int random_number = dist(engine);
    std::ostringstream oss;
    oss << std::setw(4) << std::setfill('0') << random_number;

    return oss.str();
}
