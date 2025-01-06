#include "asynctaskmanager.h"
#include "computermanager.h"
#include "windowsmessage.h"
#include "boxartmanager.h"
#include "nvpairingmanager.h"
#include "mainwindow.h"
#include "utils.h"
#include "razer.h"

#include <glog/logging.h>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/endian/conversion.hpp>

#include <future>

class PendingPairingTaskRazer
{
public:
    PendingPairingTaskRazer(NvComputer* computer, std::string pin, bool useRazerJWT)
        : m_ComputerManager(ComputerManager::getInstance()),
        m_Computer(computer),
        m_Pin(pin),
        m_useRazerJWT(useRazerJWT),
        m_pairingManager(computer)
    {
    }

    ~PendingPairingTaskRazer()
    {
        if (!m_future.valid())
            return;

        m_pairingManager.cancelGetservercert();
        m_future.get();
    }

    void pairingCompleted(NvComputer* computer, std::string error)
    {
        static_cast<void>(computer);
        m_resultString = error;
    }

    std::string getResultString()
    {
        return m_resultString;
    }

    void pair()
    {
        try {
            NvPairingManager::PairState result = m_pairingManager.pair(m_Computer->appVersion, m_Pin, m_useRazerJWT, m_Computer->serverCert);
            switch (result)
            {
            case NvPairingManager::PairState::PIN_WRONG:
                pairingCompleted(m_Computer, Razer::textMap.at("The PIN from the host PC didn't match. Please try again."));
                break;
            case NvPairingManager::PairState::FAILED:
                if (m_Computer->currentGameId != 0) {
                    pairingCompleted(m_Computer, Razer::textMap.at("You cannot pair while a previous session is still running on the host PC. Quit any running games or reboot the host PC, then try pairing again."));
                }
                else {
                    pairingCompleted(m_Computer, Razer::textMap.at("Pairing failed. Please try again."));
                }
                break;
            case NvPairingManager::PairState::ALREADY_IN_PROGRESS:
                pairingCompleted(m_Computer, Razer::textMap.at("Another pairing attempt is already in progress."));
                break;
            case NvPairingManager::PairState::PAIRED:
                // Persist the newly pinned server certificate for this host
                m_ComputerManager->saveHost(m_Computer);

                pairingCompleted(m_Computer, "");
                break;
            case NvPairingManager::PairState::RAZER_WRONG:
                pairingCompleted(m_Computer, Razer::textMap.at("Pairing failed using Razer ID. Please check the Razer ID and try again"));
                break;
            }
        } catch (const GfeHttpResponseException& e) {
            std::string msg = "Encountered an error while connecting to the host PC. Please try again.";
            pairingCompleted(m_Computer, Razer::textMap.at(msg));
            LOG(WARNING) << msg << e.toString();
        } catch (NetworkReplyException& e) {
            std::string msg = "PIN code expired.";
            pairingCompleted(m_Computer, Razer::textMap.at(msg));
            LOG(WARNING) << msg << e.toString();
        }
    }

    void asynPair()
    {
        m_future = std::async(std::launch::async, [&]() {
            pair();
        });
    }

    bool isReady()
    {
        if(!m_future.valid())
            return false;

        std::future_status status = m_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready)
            return true;

        return false;
    }

    bool waitPairStateUpdate(int maxDurationMs = 3000)
    {
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        while (NvComputer::PS_PAIRED != m_Computer->pairState )
        {
            std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
            std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);

            if (elapsed.count() >= maxDurationMs)
                return false;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

private:
    ComputerManager* m_ComputerManager;
    NvComputer* m_Computer;
    std::string m_Pin;
    bool m_useRazerJWT;
    NvPairingManager m_pairingManager;
    std::string m_resultString;
    std::future<void> m_future;
};

class DeferredHostDeletionTaskRazer
{
public:
    DeferredHostDeletionTaskRazer(NvComputer* computer)
        : m_Computer(computer),
        m_ComputerManager(ComputerManager::getInstance()) {}

    ~DeferredHostDeletionTaskRazer()
    {
        if (!m_future.valid())
            return;

        m_future.get();
    }

    void deletion()
    {
        ComputerPollingEntryRazer* pollingEntry;

        // Only do the minimum amount of work while holding the writer lock.
        // We must release it before calling saveHosts().
        {
            std::unique_lock<std::shared_mutex> locker(m_ComputerManager->m_Lock);

            auto it = m_ComputerManager->m_PollEntries.find(m_Computer->uuid);
            if (it != m_ComputerManager->m_PollEntries.end()) {
                pollingEntry = it->second;
                m_ComputerManager->m_PollEntries.erase(it);
            } else {
                pollingEntry = nullptr;
            }

            m_ComputerManager->m_KnownHosts.erase(m_Computer->uuid);
        }

        // Persist the new host list with this computer deleted
        m_ComputerManager->saveHosts();

        // Delete the polling entry first. This will stop all polling threads too.
        delete pollingEntry;

        // Delete cached box art
        BoxArtManager::deleteBoxArt(m_Computer);

        // Finally, delete the computer itself. This must be done
        // last because the polling thread might be using it.
        delete m_Computer;
    }

    void asynDeletion()
    {
        m_future = std::async(std::launch::async, [&]() {
            deletion();
        });
    }

    bool isReady()
    {
        if(!m_future.valid())
            return false;

        std::future_status status = m_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready)
            return true;

        return false;
    }

private:
    NvComputer* m_Computer;
    ComputerManager* m_ComputerManager;
    std::future<void> m_future;
};

class PendingAddTaskRazer
{

public:
    PendingAddTaskRazer(NvAddress address, NvAddress mdnsIpv6Address, bool mdns)
        : m_ComputerManager(ComputerManager::getInstance()),
        m_Address(address),
        m_MdnsIpv6Address(mdnsIpv6Address),
        m_Mdns(mdns),
        m_AboutToQuit(false),
        m_manualAddSucceed(false)
    {
    }

    ~PendingAddTaskRazer()
    {
        handleAboutToQuit();

        if(!m_future.valid())
            return;

        m_future.get();
    }

    void computerStateChanged(NvComputer* computer)
    {
        PendingAddMessage* msg = new PendingAddMessage(computer, m_ComputerManager);
        extern MyWindow g_window;
        HWND hWnd = g_window.getHandle();
        PostMessage(hWnd, msg->messageType(), reinterpret_cast<LPARAM>(nullptr), reinterpret_cast<LPARAM>(msg));
    }

    void asynAdd()
    {
        m_future = std::async(std::launch::async, [&]() {
            add();
        });
    }

    bool isReady()
    {
        if(!m_future.valid())
            return false;

        std::future_status status = m_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready)
            return true;

        return false;
    }

    bool isSucceed()
    {
        return m_manualAddSucceed;
    }

private:
    void handleAboutToQuit()
    {
        m_AboutToQuit = true;
    }

    void computerAddCompleted(bool succeed, bool detectedPortBlocking)
    {
        static_cast<void>(detectedPortBlocking);
        m_manualAddSucceed = succeed;
    }

    std::string fetchServerInfo(NvHTTP& http)
    {
        std::string serverInfo;

        // Do nothing if we're quitting
        if (m_AboutToQuit) {
            return serverInfo;
        }

        try {
            // There's a race condition between GameStream servers reporting presence over
            // mDNS and the HTTPS server being ready to respond to our queries. To work
            // around this issue, we will issue the request again after a few seconds if
            // we see a ServiceUnavailableError error.
            try {
                serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
            } catch (const NetworkReplyException& e) {
                // FIX ME: 
                //if (e.getError() == QNetworkReply::ServiceUnavailableError) {
                //    LOG(WARNING) << "Retrying request in 5 seconds after ServiceUnavailableError";
                //    std::this_thread::sleep_for(std::chrono::seconds(5));
                //    serverInfo = http.getServerInfo(NvHTTP::NVLL_VERBOSE);
                //    LOG(INFO) << "Retry successful";
                //}
                //else {
                    // Rethrow other errors
                    throw e;
                //}
            }
            return serverInfo;
        } catch (...) {
            if (!m_Mdns) {
                unsigned int portTestResult;

                portTestResult = 0;
                computerAddCompleted(false, portTestResult != 0 && portTestResult != ML_TEST_RESULT_INCONCLUSIVE);
            }
            return "";
        }
    }

    void add()
    {
        NvHTTP http(m_Address, 0, "");

        LOG(INFO) << "Processing new PC at" << m_Address.toString() << "from" << (m_Mdns ? "mDNS" : "user") << "with IPv6 address" << m_MdnsIpv6Address.toString();

        // Perform initial serverinfo fetch over HTTP since we don't know which cert to use
        std::string serverInfo = fetchServerInfo(http);
        if (serverInfo.empty() && !m_MdnsIpv6Address.isNull()) {
            // Retry using the global IPv6 address if the IPv4 or link-local IPv6 address fails
            http.setAddress(m_MdnsIpv6Address);
            serverInfo = fetchServerInfo(http);
        }
        if (serverInfo.empty()) {
            return;
        }

        // Create initial newComputer using HTTP serverinfo with no pinned cert
        NvComputer* newComputer = new NvComputer(http, serverInfo);

        // Check if we have a record of this host UUID to pull the pinned cert
        NvComputer* existingComputer;
        {
            std::shared_lock<std::shared_mutex> locker(m_ComputerManager->m_Lock);
            auto it = m_ComputerManager->m_KnownHosts.find(newComputer->uuid);
            if (it != m_ComputerManager->m_KnownHosts.end())
            {
                existingComputer = it->second;
                http.setServerCert(existingComputer->serverCert);
            }
            else
            {
                existingComputer = nullptr;
            }
        }

        // Fetch serverinfo again over HTTPS with the pinned cert
        if (existingComputer != nullptr) {
            assert(http.httpsPort() != 0);
            serverInfo = fetchServerInfo(http);
            if (serverInfo.empty()) {
                return;
            }

            // Update the polled computer with the HTTPS information
            NvComputer httpsComputer(http, serverInfo);
            newComputer->update(httpsComputer);
        }

        // Update addresses depending on the context
        if (m_Mdns) {
            // Only update local address if we actually reached the PC via this address.
            // If we reached it via the IPv6 address after the local address failed,
            // don't store the non-working local address.
            if (http.address() == m_Address) {
                newComputer->localAddress = m_Address;
            }

            // Get the WAN IP address using STUN if we're on mDNS over IPv4
            if(StringUtils::isValidIPv4(newComputer->localAddress.address())) {
                uint32_t addr;
                int err = LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &addr);
                if (err == 0) {
                    boost::asio::ip::address_v4 address(boost::endian::big_to_native(addr));
                    std::string ipStr = address.to_string();
                    newComputer->setRemoteAddress(ipStr);
                }
                else {
                    LOG(WARNING) << "STUN failed to get WAN address:" << err;
                }
            }

            if (!m_MdnsIpv6Address.isNull()) {
                std::string pp = m_MdnsIpv6Address.address();
                assert(StringUtils::isValidIPv6(m_MdnsIpv6Address.address()));
                newComputer->ipv6Address = m_MdnsIpv6Address;
            }
        }
        else {
            newComputer->manualAddress = m_Address;
        }

        boost::system::error_code ec;
        boost::asio::ip::address_v4 ip = boost::asio::ip::address_v4::from_string(m_Address.address(), ec);

        // 私有IPv4地址段的定义
        boost::asio::ip::address_v4 private_subnet_1 = boost::asio::ip::address_v4::from_string("10.0.0.0");
        boost::asio::ip::address_v4 private_subnet_2 = boost::asio::ip::address_v4::from_string("172.16.0.0");
        boost::asio::ip::address_v4 private_subnet_3 = boost::asio::ip::address_v4::from_string("192.168.0.0");

        // 检查是否在私有地址段内
        bool addressIsSiteLocalV4 = (ip.is_loopback() ||
                                     (ip.to_ulong() & private_subnet_1.to_ulong()) == private_subnet_1.to_ulong() ||
                                     (ip.to_ulong() & private_subnet_2.to_ulong()) == private_subnet_2.to_ulong() ||
                                     (ip.to_ulong() & private_subnet_3.to_ulong()) == private_subnet_3.to_ulong());

        {
            // Check if this PC already exists using opportunistic read lock
            m_ComputerManager->m_Lock.lock_shared();
            bool isLockForRead = true;
            NvComputer* existingComputer;
            auto it = m_ComputerManager->m_KnownHosts.find(newComputer->uuid);
            if (it != m_ComputerManager->m_KnownHosts.end())
            {
                existingComputer = it->second;
            }
            else
            {
                existingComputer = nullptr;
            }

            // If it doesn't already exist, convert to a write lock in preparation for updating.
            //
            // NB: ComputerManager's lock protects the host map itself, not the elements inside.
            // Those are protected by their individual locks. Since we only mutate the map itself
            // when the PC doesn't exist, we need the lock in write-mode for that case only.
            if (existingComputer == nullptr) {
                m_ComputerManager->m_Lock.unlock_shared();
                m_ComputerManager->m_Lock.lock();
                isLockForRead = false;

                // Since we had to unlock to lock for write, someone could have raced and added
                // this PC before us. We have to check again whether it already exists.
                auto it = m_ComputerManager->m_KnownHosts.find(newComputer->uuid);
                if (it != m_ComputerManager->m_KnownHosts.end())
                {
                    existingComputer = it->second;
                }
                else
                {
                    existingComputer = nullptr;
                }
            }

            if (existingComputer != nullptr) {
                // Fold it into the existing PC
                bool changed = existingComputer->update(*newComputer);
                delete newComputer;

                // Drop the lock before notifying
                if (isLockForRead) {
                    m_ComputerManager->m_Lock.unlock_shared();
                }
                else {
                    m_ComputerManager->m_Lock.unlock();
                }

                // For non-mDNS clients, let them know it succeeded
                if (!m_Mdns) {
                    computerAddCompleted(true, false);
                }

                // Tell our client if something changed
                if (changed) {
                    LOG(INFO) << existingComputer->name << "is now at" << existingComputer->activeAddress.toString();
                    computerStateChanged(existingComputer);
                }
            }
            else {
                // Store this in our active sets
                m_ComputerManager->m_KnownHosts[newComputer->uuid] = newComputer;

                // Start polling if enabled (write lock required)
                m_ComputerManager->startPollingComputer(newComputer);

                // Drop the lock before notifying
                if (isLockForRead) {
                    m_ComputerManager->m_Lock.unlock_shared();
                }
                else {
                    m_ComputerManager->m_Lock.unlock();
                }

                // If this wasn't added via mDNS but it is a RFC 1918 IPv4 address and not a VPN,
                // go ahead and do the STUN request now to populate an external address.
                if (!m_Mdns && addressIsSiteLocalV4 && newComputer->getActiveAddressReachability() != NvComputer::RI_VPN) {
                    uint32_t addr;
                    int err = LiFindExternalAddressIP4("stun.moonlight-stream.org", 3478, &addr);
                    if (err == 0) {
                        boost::asio::ip::address_v4 address(boost::endian::big_to_native(addr));
                        std::string ipStr = address.to_string();
                        newComputer->setRemoteAddress(ipStr);
                    }
                    else {
                        LOG(WARNING) << "STUN failed to get WAN address:" << err;
                    }
                }

                // For non-mDNS clients, let them know it succeeded
                if (!m_Mdns) {
                    computerAddCompleted(true, false);
                }

                // Tell our client about this new PC
                computerStateChanged(newComputer);
            }
        }
    }

    ComputerManager* m_ComputerManager;
    NvAddress m_Address;
    NvAddress m_MdnsIpv6Address;
    bool m_Mdns;
    bool m_AboutToQuit;
    bool m_manualAddSucceed;
    std::future<void> m_future;
};

class PendingQuitTaskRazer
{
public:
    PendingQuitTaskRazer(NvComputer* computer)
        : m_Computer(computer)
    {
    }

    ~PendingQuitTaskRazer()
    {
        if (!m_future.valid())
            return;

        m_future.get();
    }

    void quitAppFailed(std::string error)
    {
        resultString = error;
    }

    std::string getResultString()
    {
        return resultString;
    }

    void asynQuit()
    {
        m_future = std::async(std::launch::async, [&]() {
            quit();
            });
    }

    bool isReady()
    {
        if (!m_future.valid())
            return false;

        std::future_status status = m_future.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready)
            return true;

        return false;
    }

    bool waitCurrentAppUpdate(int maxDurationMs = 3000)
    {
        std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();
        while (0 != m_Computer->currentGameId)
        {
            std::chrono::steady_clock::time_point currentTime = std::chrono::steady_clock::now();
            std::chrono::milliseconds elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);

            if (elapsed.count() >= maxDurationMs)
                return false;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return true;
    }

private:
    void quit()
    {
        NvHTTP http(m_Computer);

        try
        {
            if (m_Computer->currentGameId != 0)
            {
                http.quitApp();
            }
            else // Maybe the game is exited
            {
                std::unique_lock<std::shared_mutex> locker(m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
        }
        catch (const GfeHttpResponseException& e)
        {
            {
                std::unique_lock<std::shared_mutex> locker(m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            if (e.getStatusCode() == 599)
            {
                // 599 is a special code we make a custom message for
                quitAppFailed(Razer::textMap.at("The game wasn't started on this PC. You must quit the game on the host PC manually or use the device that originally started the game."));
            }
            else
            {
                if (5034 == e.getStatusCode())
                    quitAppFailed("remote_play_host_quit_failed_1");

                //std::string msg = "Host return error.";
                //quitAppFailed(msg);
                //LOG(WARNING) << msg << e.toString();
            }
        }
        catch (const NetworkReplyException& e)
        {
            {
                std::unique_lock<std::shared_mutex> locker(m_Computer->lock);
                m_Computer->pendingQuit = false;
            }
            quitAppFailed(Razer::textMap.at("Network error."));
        }
    }

    NvComputer* m_Computer;
    std::string resultString;
    std::future<void> m_future;
};

void AsyncTaskManager::stopAsyncTask()
{
    for(auto it = m_PairEntries.begin(); it != m_PairEntries.end(); ++it)
    {
        delete it->second;
    }

    for(auto it = m_DeletionEntries.begin(); it != m_DeletionEntries.end(); ++it)
    {
        delete it->second;
    }

    for(auto it = m_AddEntries.begin(); it != m_AddEntries.end(); ++it)
    {
        delete it->second;
    }

    for (auto it = m_APPQuitEntries.begin(); it != m_APPQuitEntries.end(); ++it)
    {
        delete it->second;
    }
}

std::string AsyncTaskManager::startupPairTask(NvComputer* computer, std::string pin, bool useRazerJWT)
{
    std::string taskId = Uuid::createUuidWithHyphens();

    PendingPairingTaskRazer* pair = new PendingPairingTaskRazer(computer, pin, useRazerJWT);
    pair->asynPair();

    {
        std::lock_guard<std::mutex> locker(m_pairMtx);
        m_PairEntries.insert(std::make_pair(taskId, pair));
    }

    return taskId;
}

bool AsyncTaskManager::getPairTaskResult(const std::string& taskId, Result& result)
{
    std::lock_guard<std::mutex> locker(m_pairMtx);

    auto it = m_PairEntries.find(taskId);
    if(it == m_PairEntries.end())
        return false;

    if(!it->second->isReady())
    {
        result.isCompleted = false;
        result.isSucceed = false;
        result.errorString.clear();
        return true;
    }

    std::string resultString = it->second->getResultString();
    result.isCompleted = true;
    result.isSucceed = resultString.empty();
    result.errorString = resultString;

    // When pairing is successful, wait for the local host status to change
    // before returning to avoid obtaining an out-of-date host status.
    if(result.isSucceed)
        it->second->waitPairStateUpdate();

    return true;
}

bool AsyncTaskManager::cancelPairTask(const std::string& taskId, Result& result)
{
    std::lock_guard<std::mutex> locker(m_pairMtx);

    auto it = m_PairEntries.find(taskId);
    if (it == m_PairEntries.end())
        return false;

    delete it->second;
    m_PairEntries.erase(it);

    result.isCompleted = true;
    result.isSucceed = true;
    result.errorString.clear();

    return true;
}

std::string AsyncTaskManager::startupDeleteTask(NvComputer* computer)
{
    std::string taskId = Uuid::createUuidWithHyphens();

    DeferredHostDeletionTaskRazer* deletion = new DeferredHostDeletionTaskRazer(computer);
    deletion->asynDeletion();

    {
        std::lock_guard<std::mutex> locker(m_deleteMtx);
        m_DeletionEntries.insert(std::make_pair(taskId, deletion));
    }

    return taskId;
}

bool AsyncTaskManager::getDeleteTaskResult(const std::string& taskId, Result& result)
{
    {
        std::lock_guard<std::mutex> locker(m_deleteMtx);

        auto it = m_DeletionEntries.find(taskId);
        if(it == m_DeletionEntries.end())
            return false;

        if(!it->second->isReady())
        {
            result.isCompleted = false;
            result.isSucceed = false;
            result.errorString.clear();
            return true;
        }
    }

    result.isCompleted = true;
    result.isSucceed = true;
    result.errorString.clear();

    return true;
}

std::string AsyncTaskManager::startupAddTask(NvAddress address, NvAddress mdnsIpv6Address, bool mdns)
{
    std::string taskId = Uuid::createUuidWithHyphens();
    PendingAddTaskRazer* add = new PendingAddTaskRazer(address, mdnsIpv6Address, mdns);
    add->asynAdd();

    {
        std::lock_guard<std::mutex> locker(m_addMtx);
        m_AddEntries.insert(std::make_pair(taskId, add));
    }

    return taskId;
}

bool AsyncTaskManager::getAddTaskResult(const std::string& taskId, Result& result)
{
    std::lock_guard<std::mutex> locker(m_addMtx);

    auto it = m_AddEntries.find(taskId);
    if(it == m_AddEntries.end())
        return false;

    if(!it->second->isReady())
    {
        result.isCompleted = false;
        result.isSucceed = false;
        result.errorString.clear();
        return true;
    }

    result.isCompleted = true;
    result.isSucceed = it->second->isSucceed();
    result.errorString.clear();

    return true;
}

std::string AsyncTaskManager::startupAPPQuitTask(NvComputer* computer)
{
    std::string taskId = Uuid::createUuidWithHyphens();
    PendingQuitTaskRazer* APPQuit = new PendingQuitTaskRazer(computer);
    APPQuit->asynQuit();

    {
        std::lock_guard<std::mutex> locker(m_appQuitMtx);
        m_APPQuitEntries.insert(std::make_pair(taskId, APPQuit));
    }

    return taskId;
}

bool AsyncTaskManager::getAPPQuitTaskResult(const std::string& taskId, Result& result)
{
    std::lock_guard<std::mutex> locker(m_appQuitMtx);

    auto it = m_APPQuitEntries.find(taskId);
    if (it == m_APPQuitEntries.end())
        return false;

    if (!it->second->isReady())
    {
        result.isCompleted = false;
        result.isSucceed = false;
        result.errorString.clear();
        return true;
    }

    std::string resultString = it->second->getResultString();
    result.isCompleted = true;
    result.isSucceed = resultString.empty();
    result.errorString = resultString;

    // After the application quits successfully, wait for the local application status 
    // to change before returning to avoid obtaining outdated application status.
    if (result.isSucceed)
        it->second->waitCurrentAppUpdate();

    return true;
}

