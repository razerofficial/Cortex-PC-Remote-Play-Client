#pragma once

#include <map>
#include <string>
#include <mutex>

class NvAddress;
class NvComputer;
class PendingPairingTaskRazer;
class DeferredHostDeletionTaskRazer;
class PendingAddTaskRazer;
class PendingQuitTaskRazer;

class AsyncTaskManager
{
public:
    struct Result
    {
        Result():isCompleted(false),isSucceed(false),errorString(""){}

        bool isCompleted;
        bool isSucceed;
        std::string errorString;
    };

    void stopAsyncTask();

    std::string startupPairTask(NvComputer* computer, std::string pin, bool useRazerJWT);
    bool getPairTaskResult(const std::string& taskId, Result& result);
    bool cancelPairTask(const std::string& taskId, Result& result);

    std::string startupDeleteTask(NvComputer* computer);
    bool getDeleteTaskResult(const std::string& taskId, Result& result);

    std::string startupAddTask(NvAddress address, NvAddress mdnsIpv6Address, bool mdns);
    bool getAddTaskResult(const std::string& taskId, Result& result);

    std::string startupAPPQuitTask(NvComputer* computer);
    bool getAPPQuitTaskResult(const std::string& taskId, Result& result);

private:
    std::mutex m_pairMtx;
    std::map<std::string, PendingPairingTaskRazer *> m_PairEntries;

    std::mutex m_deleteMtx;
    std::map<std::string, DeferredHostDeletionTaskRazer *> m_DeletionEntries;

    std::mutex m_addMtx;
    std::map<std::string, PendingAddTaskRazer *> m_AddEntries;

    std::mutex m_appQuitMtx;
    std::map<std::string, PendingQuitTaskRazer*> m_APPQuitEntries;
};
