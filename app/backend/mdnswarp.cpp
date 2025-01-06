#include <winsock2.h>
#include <chrono>
#include <glog/logging.h>

#include "MDNSWarp.h"

MDNSWarp::MDNSWarp(const std::string& type)
    : m_type(type)
    , m_interruptionRequested(false)
{

}

MDNSWarp::~MDNSWarp()
{
    if (!isInterruptionRequested())
        requestInterruption();

    if (m_thread.joinable())
        m_thread.join();
}

void MDNSWarp::requestInterruption()
{
    std::lock_guard<std::mutex> lock(m_interruptionMutex);
    m_interruptionRequested = true;
}

bool MDNSWarp::isInterruptionRequested()
{
    std::lock_guard<std::mutex> lock(m_interruptionMutex);
    return m_interruptionRequested;
}

void MDNSWarp::resolvedHost(std::function<void(mdns_cpp::mDNS::mdns_out)> callback)
{
    m_callback = callback;
}

void MDNSWarp::query()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData)) {
        LOG(ERROR) << "Failed to initialize WinSock" << std::endl;
        return;
    }

    mdns_cpp::mDNS mdns;
    while (!isInterruptionRequested())
    {
        try
        {
            std::vector<mdns_cpp::mDNS::mdns_out> out = mdns.executeQuery(m_type);

            for (const mdns_cpp::mDNS::mdns_out& host : out)
            {
                if (host.ipv4.empty())
                    continue;

                m_callback(host);
            }
        }
        catch (const std::exception& e)
        {
            LOG(ERROR) << "mDNS error:" << e.what();
        }

        for (int i = 0; i < 600 && !isInterruptionRequested(); i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    WSACleanup();
}

void MDNSWarp::start()
{
    m_thread = std::thread(&MDNSWarp::query, this);
}

void MDNSWarp::updateCacheList(std::vector<mdns_cpp::mDNS::mdns_out> newList)
{
    // Process updates first
    for (int i = 0; i < m_cache.size(); i++)
    {
        const mdns_cpp::mDNS::mdns_out& existingHost = m_cache.at(i);
        bool found = false;
        for (const mdns_cpp::mDNS::mdns_out& newHost : newList)
        {
            if (existingHost.srv_name == newHost.srv_name)
            {
                // If the data changed, update it in our list
                if (existingHost != newHost)
                {
                    m_cache[i] = newHost;
                    m_callback(newHost);
                }

                found = true;
                break;
            }
        }

        if (!found) {
            m_cache.erase(m_cache.begin() + i);
            i--;
        }
    }

    // Process additions now
    for (const mdns_cpp::mDNS::mdns_out& newHost : newList)
    {
        bool found = false;

        for (int i = 0; i < m_cache.size(); i++)
        {
            const mdns_cpp::mDNS::mdns_out& existingHost = m_cache.at(i);

            if (existingHost.srv_name == newHost.srv_name)
            {
                found = true;
                break;
            }
        }

        if (!found) 
        {
            m_cache.push_back(newHost);
            m_callback(newHost);
        }
    }
}
