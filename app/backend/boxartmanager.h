#pragma once

#include "computermanager.h"

class ThreadPool;

class BoxArtManager
{

    friend class NetworkBoxArtLoadTask;

public:
    explicit BoxArtManager();
    ~BoxArtManager();

    std::string
    loadBoxArt(NvComputer* computer, NvApp& app);

    static
    void
    deleteBoxArt(NvComputer* computer);

private:
    std::string
    loadBoxArtFromNetwork(NvComputer* computer, int appId);

    std::string
    getFilePathForBoxArt(NvComputer* computer, int appId);

    std::filesystem::path m_BoxArtDir;
    ThreadPool* m_threadPool;
};
