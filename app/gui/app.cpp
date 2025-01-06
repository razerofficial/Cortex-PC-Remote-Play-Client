#include "app.h"
#include "backend/razer.h"
#include "streaming/session.h"
#include <boost/algorithm/string.hpp>

App::App()
    : m_Computer(nullptr)
    , m_ComputerManager(ComputerManager::getInstance())
    , m_CurrentGameId(0)
    , m_ShowHiddenGames(false)
{
}

void App::initialize(const std::string& uuid, bool showHiddenGames)
{
    assert(!uuid.empty());
    std::vector<NvComputer*> computers = m_ComputerManager->getComputers();
    for (NvComputer* computer : computers)
    {
        if (computer->uuid == uuid)
        {
            m_Computer = computer;
            m_CurrentGameId = computer->currentGameId;
            break;
        }
    }
    if (nullptr == m_Computer)
        return;

    m_ShowHiddenGames = showHiddenGames;
    updateAppList(m_Computer->appList);
}

std::vector<NvApp> App::getVisibleApps()
{
    if ((NvComputer::CS_ONLINE != m_Computer->state ||
        NvComputer::PS_PAIRED != m_Computer->pairState) && !Session::isBusy())
        return std::vector<NvApp>();

    return m_VisibleApps;
}

int App::getRunningAppId()
{
    if (nullptr == m_Computer)
        return 0;
    return m_Computer->currentGameId;
}

std::string App::getAppBoxArt(NvApp& app)
{
    return m_BoxArtManager.loadBoxArt(m_Computer, app);
}

bool App::startQuiting(std::string& taskId, std::string& errorString)
{
    if (nullptr == m_Computer)
    {
        errorString = Razer::textMap.at("The specified host does not exist!");
        return false;
    }

    taskId = ComputerManager::getInstance()->quitRunningApp(m_Computer);
    return true;
}

bool App::getQuitTaskResult(const std::string& taskId, AsyncTaskManager::Result& result)
{
    return ComputerManager::getInstance()->getQuitTaskResult(taskId, result);
}

void App::setAppHidden(int appId, bool hidden)
{
    {
        std::unique_lock<std::shared_mutex> locker(m_Computer->lock);

        for (NvApp& app : m_Computer->appList) 
        {
            if (m_Computer->currentGameId != app.id || app.hidden)
            {
                if (app.id == appId)
                {
                    app.hidden = hidden;
                    break;
                }
            }
        }
    }

    m_ComputerManager->clientSideAttributeUpdated(m_Computer);
}
//test for app begin
#include <glog/logging.h>
//test for app end
void App::updateAppList(std::vector<NvApp> newList)
{
    m_AllApps = newList;

    std::vector<NvApp> newVisibleList = getVisibleApps(newList);

    // Define a custom comparator for sorting and insertion
    auto compareNvApps = [](const NvApp& app1, const NvApp& app2) {
        // Step 1: "Desktop" application should be first
        if (app1.name == "Desktop") return true;
        if (app2.name == "Desktop") return false;

        // Step 2: Sort by lastAppStartTime in descending order
        if (app1.lastAppStartTime != app2.lastAppStartTime) {
            return app1.lastAppStartTime > app2.lastAppStartTime;
        }

        // Step 3: For applications with lastAppStartTime == 0, sort by name in ascending order
        if (app1.lastAppStartTime == 0 && app2.lastAppStartTime == 0) {
            std::string appName1 = app1.name;
            std::string appName2 = app2.name;

            boost::algorithm::to_lower(appName1);
            boost::algorithm::to_lower(appName2);

            return appName1 < appName2;
        }

        // If all else fails, maintain the current order
        return false;
        };

    // Process removals and updates first
    for (int i = m_VisibleApps.size() - 1; i >= 0; --i)
    {
        const NvApp& existingApp = m_VisibleApps.at(i);
        auto it = std::find_if(newVisibleList.begin(), newVisibleList.end(),
            [&existingApp](const NvApp& app) { return existingApp.id == app.id; });

        if (it != newVisibleList.end())
        {
            // If the data changed, update it in our list
            if (existingApp != *it)
            {
                m_VisibleApps[i] = *it;
            }
        }
        else
        {
            // The app was removed from the new list, so remove it from the visible list
            m_VisibleApps.erase(m_VisibleApps.begin() + i);
        }
    }

    // Insert remaining apps from newVisibleList into m_VisibleApps at the correct position
    for (const NvApp& newApp : newVisibleList)
    {
        // Skip already processed apps
        if (std::find(m_VisibleApps.begin(), m_VisibleApps.end(), newApp) != m_VisibleApps.end()) {
            continue;
        }

        auto insertionIt = std::upper_bound(m_VisibleApps.begin(), m_VisibleApps.end(), newApp, compareNvApps);

        // Insert into m_VisibleApps
        m_VisibleApps.insert(insertionIt, newApp);
    }

    //test for app begin
    if (newVisibleList != m_VisibleApps)
    {
        std::stringstream ss;
        for (const NvApp& App : newVisibleList)
            ss << App.id << " time: " << App.lastAppStartTime << " guid: "
            << App.guid << " name: " << App.name << " platform: " << App.gamePlatform
            << " hiden:" << App.hidden << " launch: " << App.directLaunch;
        ss << "\nm_VisibleApps: \n";
        for (const NvApp& App : m_VisibleApps)
            ss << App.id << " time: " << App.lastAppStartTime << " guid: "
            << App.guid << " name: " << App.name << " platform: " << App.gamePlatform
            << " hiden:" << App.hidden << " launch: " << App.directLaunch;

        LOG(ERROR) << ss.str();
    }
    //test for app begin
    assert(newVisibleList == m_VisibleApps);
}

std::vector<NvApp> App::getVisibleApps(const std::vector<NvApp>& appList)
{
    std::vector<NvApp> visibleApps;

    for (const NvApp& app : appList) {
        // Don't immediately hide games that were previously visible. This
        // allows users to easily uncheck the "Hide App" checkbox if they
        // check it by mistake.
        if (m_ShowHiddenGames || !app.hidden || isAppCurrentlyVisible(app)) {
            visibleApps.push_back(app);
        }
    }

    return visibleApps;
}

bool App::isAppCurrentlyVisible(const NvApp& app)
{
    for (const NvApp& visibleApp : m_VisibleApps) {
        if (app.id == visibleApp.id) {
            return true;
        }
    }

    return false;
}