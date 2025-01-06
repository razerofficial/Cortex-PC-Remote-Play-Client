#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <functional>

#include <mdns_cpp/mdns.hpp>

class MDNSWarp
{

public:
    MDNSWarp(const std::string& type);
    ~MDNSWarp();

    void resolvedHost(std::function<void(mdns_cpp::mDNS::mdns_out)> callback);
    void start();

private:
    void query();
    void updateCacheList(std::vector<mdns_cpp::mDNS::mdns_out> newList);

    void requestInterruption();
    bool isInterruptionRequested();

private:
    std::string m_type;
    std::function<void(mdns_cpp::mDNS::mdns_out)> m_callback;

    std::vector<mdns_cpp::mDNS::mdns_out> m_cache;
    std::mutex m_interruptionMutex;
    bool m_interruptionRequested;
    std::thread m_thread;
};
