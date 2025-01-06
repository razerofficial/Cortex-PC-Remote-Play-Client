#pragma once

#include <string>
#include "backend/asynctaskmanager.h"

class Stream
{
public:
    Stream() = default;
    Stream(const std::string& uuid, int appID);
    ~Stream();

    bool startStreaming(std::string& errorString);

    bool getStreamTaskResult(AsyncTaskManager::Result& result);

private:
    std::string m_uuid;
    int m_appID;
};
