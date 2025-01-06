#pragma once

#include <string>
#include "backend/asynctaskmanager.h"

class DeleteComputer
{
public:
    DeleteComputer() = default;
    DeleteComputer(const std::string& uuid);

    bool startDeleting(std::string& taskId, std::string& errorString);
    bool getDeleteTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

private:
    std::string m_uuid;
};
