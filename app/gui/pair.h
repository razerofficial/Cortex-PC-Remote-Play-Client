#pragma once

#include <string>
#include "backend/asynctaskmanager.h"

class Pair
{
public:
    Pair() = default;
    Pair(const std::string& uuid, const std::string& pin, bool useRazerJWT);

    bool razerIDAvailability(const std::string& uuid, std::string& errorString);
    bool startPairing(std::string& taskId, std::string& errorString);
    bool getPairTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);
    bool cancelPairing(const std::string& taskId, AsyncTaskManager::Result& result);

private:
    std::string m_uuid;
    std::string m_pin;
    bool m_useRazerJWT;
};
