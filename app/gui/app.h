#pragma once

#include "backend/boxartmanager.h"
#include "backend/computermanager.h"

class App
{
public:
    explicit App();

    void initialize(const std::string& uuid, bool showHiddenGames);

    std::vector<NvApp> getVisibleApps();
    int getRunningAppId();
    std::string getAppBoxArt(NvApp& app);

    bool startQuiting(std::string& taskId, std::string& errorString);
    bool getQuitTaskResult(const std::string& taskId, AsyncTaskManager::Result& result);

    void setAppHidden(int appId, bool hidden);

private:
    void updateAppList(std::vector<NvApp> newList);

    std::vector<NvApp> getVisibleApps(const std::vector<NvApp>& appList);

    bool isAppCurrentlyVisible(const NvApp& app);

    NvComputer* m_Computer;
    BoxArtManager m_BoxArtManager;
    ComputerManager* m_ComputerManager;
    std::vector<NvApp> m_VisibleApps, m_AllApps;
    int m_CurrentGameId;
    bool m_ShowHiddenGames;
};
