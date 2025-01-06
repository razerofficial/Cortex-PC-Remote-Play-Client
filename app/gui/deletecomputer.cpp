#include "deletecomputer.h"
#include "backend/razer.h"
#include "backend/computermanager.h"


DeleteComputer::DeleteComputer(const std::string& uuid)
    : m_uuid(uuid)
{
}

bool DeleteComputer::startDeleting(std::string& taskId, std::string& errorString)
{
    NvComputer* deleteComputer = nullptr;
    std::vector<NvComputer*> computers = ComputerManager::getInstance()->getComputers();
    for(int i = 0; i < computers.size(); i++)
    {
        if(m_uuid == computers.at(i)->uuid)
        {
            deleteComputer = computers.at(i);
            break;
        }
    }

    if(nullptr == deleteComputer)
    {
        errorString = Razer::textMap.at("The specified host does not exist!");
        return false;
    }

    taskId = ComputerManager::getInstance()->deleteHost(deleteComputer);
    return true;
}

bool DeleteComputer::getDeleteTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->getDeleteTaskResult(taskId, result);
}
