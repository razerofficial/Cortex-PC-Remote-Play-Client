#pragma once

#include "nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "settings/compatfetcher.h"
#include "asynctaskmanager.h"
#include "mdnswarp.h"

#include <map>
#include <unordered_map>


class ComputerManager;
class Session;
class HTTPServer;
class HeartBeat;

class DelayedFlushThreadRazer
{
public:
    DelayedFlushThreadRazer(ComputerManager* cm)
        : m_ComputerManager(cm)
        , m_interruptionRequested(false)
        , m_isRunning(false)
    {
    }

    ~DelayedFlushThreadRazer()
    {
        if(!isInterruptionRequested())
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

    void run();

    void start()
    {
        m_thread = std::thread(&DelayedFlushThreadRazer::run, this);
    }

    void wait()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

    bool isRunning()
    {
        return m_isRunning;
    }

private:
    ComputerManager* m_ComputerManager;
    std::mutex m_interruptionMutex;
    bool m_interruptionRequested;
    std::thread m_thread;
    bool m_isRunning;
};

class PcMonitorThreadInterfaceRazer
{
public:
    PcMonitorThreadInterfaceRazer()
        : m_interruptionRequested(false)
        , m_isFinished(false)
    {
    }

    virtual ~PcMonitorThreadInterfaceRazer()
    {
        if(!isInterruptionRequested())
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

    bool isFinished()
    {
        return m_isFinished;
    }

    void wait()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    virtual void run() = 0;

protected:
    std::mutex m_interruptionMutex;
    bool m_interruptionRequested;
    std::thread m_thread;
    bool m_isFinished;
};

class ComputerPollingEntryRazer
{
public:
    ComputerPollingEntryRazer()
        : m_ActiveThread(nullptr)
    {

    }

    virtual ~ComputerPollingEntryRazer()
    {
        interrupt();

        // interrupt() should have taken care of this
        assert(m_ActiveThread == nullptr);

        for (PcMonitorThreadInterfaceRazer* thread : m_InactiveList) {
            thread->wait();
            delete thread;
        }
    }

    bool isActive()
    {
        cleanInactiveList();

        return m_ActiveThread != nullptr;
    }

    void setActiveThread(PcMonitorThreadInterfaceRazer* thread)
    {
        cleanInactiveList();

        assert(!isActive());
        m_ActiveThread = thread;
    }

    void interrupt()
    {
        cleanInactiveList();

        if (m_ActiveThread != nullptr) {
            // Interrupt the active thread
            m_ActiveThread->requestInterruption();

            // Place it on the inactive list awaiting death
            m_InactiveList.push_back(m_ActiveThread);

            m_ActiveThread = nullptr;
        }
    }

private:
    void cleanInactiveList()
    {
        for(auto it = m_InactiveList.begin(); it != m_InactiveList.end();)
        {
            if ((*it)->isFinished())
            {
                it = m_InactiveList.erase(it);
            } else {
                ++it;
            }
        }
    }

    PcMonitorThreadInterfaceRazer* m_ActiveThread;
    std::list<PcMonitorThreadInterfaceRazer*> m_InactiveList;
};

class ComputerManager
{
    friend class DeferredHostDeletionTaskRazer;
    friend class PendingAddTaskRazer;
    friend class PendingPairingTaskRazer;
    friend class DelayedFlushThreadRazer;

public:
    static ComputerManager* getInstance();

    static void destroyInstance();

    void startPolling();

    void stopPollingAsync();

    std::string addNewHostManually(std::string address);

    std::string addNewHost(NvAddress address, bool mdns, NvAddress mdnsIpv6Address = NvAddress());

    bool getAddTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

    std::string generatePinString();

    std::string pairHost(NvComputer* computer, std::string pin, bool useRazerJWT);

    bool getPairTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

    bool cancelPairTask(const std::string& taskId, AsyncTaskManager::Result& result);

    std::string quitRunningApp(NvComputer* computer);

    bool getQuitTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

    std::vector<NvComputer*> getComputers();

    // computer is deleted inside this call
    std::string deleteHost(NvComputer* computer);

    bool getDeleteTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

    void streamHost(NvComputer* computer,const NvApp& app);

    bool getStreamTaskResult(AsyncTaskManager::Result& result);

    void exitMessageLoop();

    bool isExiting();

    void renameHost(NvComputer* computer, std::string name);

    void clientSideAttributeUpdated(NvComputer* computer);

    void handleComputerStateChanged(NvComputer* computer);

    void handleMdnsServiceResolved(mdns_cpp::mDNS::mdns_out mdnsOut);

private:
    ComputerManager(StreamingPreferences* prefs);
    virtual ~ComputerManager();
    ComputerManager(const ComputerManager&) = delete;
    ComputerManager& operator=(const ComputerManager&) = delete;
    static ComputerManager* s_instance;
    static std::once_flag s_initFlag;

    void saveHosts();

    void saveHost(NvComputer* computer);

    // QHostAddress getBestGlobalAddressV6(std::vector<QHostAddress>& addresses);

    void startPollingComputer(NvComputer* computer);

    StreamingPreferences* m_Prefs;
    int m_PollingRef;
    std::shared_mutex m_Lock;
    std::map<std::string, NvComputer*> m_KnownHosts;
    std::map<std::string, ComputerPollingEntryRazer*> m_PollEntries;
    std::unordered_map<std::string, NvComputer> m_LastSerializedHosts;  // Protected by m_DelayedFlushMutex
    std::shared_ptr<MDNSWarp> m_Mdns;
    DelayedFlushThreadRazer* m_DelayedFlushThread;
    std::mutex m_DelayedFlushMutex; // Lock ordering: Must never be acquired while holding NvComputer lock
    std::condition_variable m_DelayedFlushCondition;
    bool m_NeedsDelayedFlush;

    HeartBeat* m_heartBeat;
    HTTPServer* m_server;
    AsyncTaskManager* m_asynsTasks;
    bool m_readyQuit;
};
