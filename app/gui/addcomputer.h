#pragma once

#include <string>
#include "backend/asynctaskmanager.h"

class AddComputer
{
public:
    AddComputer() = default;
    AddComputer(const std::string& address);

    bool startAdding(std::string& taskId, std::string& errorString);
    bool getAddTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

private:
    std::string m_address;
};
