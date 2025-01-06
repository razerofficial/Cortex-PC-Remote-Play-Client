#include "pair.h"
#include "backend/razer.h"
#include "backend/computermanager.h"

Pair::Pair(const std::string& uuid, const std::string& pin, bool useRazerJWT)
    : m_uuid(uuid)
    , m_pin(pin)
    , m_useRazerJWT(useRazerJWT)
{
}

bool Pair::razerIDAvailability(const std::string& uuid, std::string& errorString)
{
    NvComputer* pairComputer = nullptr;
    std::vector<NvComputer*> computers = ComputerManager::getInstance()->getComputers();
    for (NvComputer* computer : computers)
    {
        if (computer->uuid == uuid)
        {
            pairComputer = computer;
            break;
        }
    }

    if (nullptr == pairComputer)
    {
        errorString = Razer::textMap.at("The specified host does not exist!");
        return false;
    }
    else if (!pairComputer->useSameRazerID)
    {
        errorString = Razer::textMap.at("The paired Razer ID does not match!");
        return false;
    }
    else if (NvComputer::RP_DISABLE == pairComputer->razerPairMode)
    {
        errorString = Razer::textMap.at("Razer ID pairing has been disabled by the host PC!");
        return false;
    }
    else if (NvComputer::RP_UNKNOWN == pairComputer->razerPairMode)
    {
        // FIX ME: report other language
        errorString = Razer::textMap.at("Failed to obtain host Razer ID pairing mode!");
        return false;
    }

    return true;
}

bool Pair::startPairing(std::string& taskId, std::string& errorString)
{
    NvComputer* pairComputer = nullptr;
    std::vector<NvComputer*> computers = ComputerManager::getInstance()->getComputers();
    for(NvComputer* computer : computers)
    {
        if(computer->uuid == m_uuid)
        {
            pairComputer = computer;
            break;
        }
    }

    if(nullptr == pairComputer)
    {
        errorString = Razer::textMap.at("The specified host does not exist!");
        return false;
    }
    else if(NvComputer::CS_ONLINE != pairComputer->state)
    {
        errorString = Razer::textMap.at("The specified host is offline!");
        return false;
    }
    else if(NvComputer::PS_NOT_PAIRED != pairComputer->pairState)
    {
        errorString = Razer::textMap.at("The specified host is already paired!");
        return false;
    }

    taskId = ComputerManager::getInstance()->pairHost(pairComputer, m_pin, m_useRazerJWT);

    return true;
}

bool Pair::getPairTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->getPairTaskResult(taskId, result);
}

bool Pair::cancelPairing(const std::string& taskId, AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->cancelPairTask(taskId, result);
}
