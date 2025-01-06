#include "addcomputer.h"
#include "utils.h"
#include "backend/razer.h"
#include "backend/computermanager.h"


AddComputer::AddComputer(const std::string& address)
    : m_address(address)
{
}

bool AddComputer::startAdding(std::string& taskId, std::string& errorString)
{
    if(!StringUtils::isValidIPv4(m_address) && !StringUtils::isValidIPv6(m_address))
    {
        errorString = Razer::textMap.at("Could not connect to the Host.");
        return false;
    }

    taskId = ComputerManager::getInstance()->addNewHostManually(m_address);
    return true;
}

bool AddComputer::getAddTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->getAddTaskResult(taskId, result);
}
