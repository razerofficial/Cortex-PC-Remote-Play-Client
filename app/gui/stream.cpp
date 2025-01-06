#include "stream.h"
#include "utils.h"
#include "backend/razer.h"
#include "backend/computermanager.h"
#include "streaming/session.h"


Stream::Stream(const std::string& uuid, int appID)
    : m_uuid(uuid)
    , m_appID(appID)
{

}

Stream::~Stream()
{

}

bool Stream::startStreaming(std::string& errorString)
{
    NvApp streamApp;
    NvComputer* streamComputer = nullptr;
    std::vector<NvComputer*> computers = ComputerManager::getInstance()->getComputers();
    for(NvComputer* computer : computers)
    {
        if(computer->uuid == m_uuid)
        {
            streamComputer = computer;
            for(const NvApp& app : computer->appList)
            {
                if(app.id == m_appID)
                {
                    streamApp = app;
                    break;
                }
            }
            break;
        }
    }

    if(nullptr == streamComputer)
    {
        errorString = Razer::textMap.at("The specified host PC does not exist!");
        return false;
    }
    else if(NvComputer::CS_ONLINE != streamComputer->state)
    {
        errorString = Razer::textMap.at("The specified host PC is offline!");
        return false;
    }
    else if(NvComputer::PS_PAIRED != streamComputer->pairState)
    {
        errorString = Razer::textMap.at("The specified host PC is not paired!");
        return false;
    }
    else if(!streamApp.isInitialized())
    {
        errorString = Razer::textMap.at("The specified application does not exist!");
        return false;
    }
    else if (!Session::tryAcquireSessionControl())
    {
        errorString = Razer::textMap.at("Remote Play is currently streaming.");
        return false;
    }

    {
        streamApp.lastAppStartTime = DateTime::currentSecsSinceEpoch();
        std::unique_lock<std::shared_mutex> locker(streamComputer->lock);
        streamComputer->updateApp(streamApp);
    }

    ComputerManager::getInstance()->streamHost(streamComputer, streamApp);
    return true;
}

bool Stream::getStreamTaskResult(AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->getStreamTaskResult(result);
}

