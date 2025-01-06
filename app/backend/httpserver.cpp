#include "httpserver.h"
#include "gui/computer.h"
#include "gui/app.h"
#include "gui/pair.h"
#include "gui/stream.h"
#include "gui/settings.h"
#include "gui/addcomputer.h"
#include "gui/deletecomputer.h"
#include "gui/quit.h"
#include "razer.h"
#include "utils.h"
#include "settings/configuer.h"
#include "systemproperties.h"

#include <json.hpp>
#include <glog/logging.h>

#define ACCESS_CONTROL_ALLOW_ORIGIN "*"
#define ACCESS_CONTROL_ALLOW_METHODS "GET, POST, PUT, DELETE, OPTIONS"
#define ACCESS_CONTROL_ALLOW_HEADERS "Content-Type"


class RequestLogger
{
public:
    void logRequest(const std::shared_ptr<Server::Request>& request, SimpleWeb::StatusCode code, const std::string& response = "")
    {
        std::ostringstream ss;
        ss << "\nHTTP request: "
            << request->method << ' ' << request->path << '/' << request->query_string << ' '
            << request->content.string() + (request->content.string().empty() ? "" : "\n")
            << static_cast<int>(code) << ' ' << response;

        LOG(INFO) << ss.str();
    }
};

HTTPServer::HTTPServer()
{
    m_server.config.port = Configure::getInstance()->getGeneral<int>(SER_UIHTTPPORT);
    m_reqLogger = new RequestLogger();

    m_server.default_resource["OPTIONS"] = [](std::shared_ptr<Server::Response> response, std::shared_ptr<Server::Request> request) {
        *response << "HTTP/1.1 204 No Content\r\n"
                  << "Access-Control-Allow-Origin: *\r\n" // set CORS
                  << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n" // set allow method
                  << "Access-Control-Allow-Headers: Content-Type\r\n" // set allow header
                  << "Content-Length: 0\r\n"
                  << "\r\n";
    };

    m_server.resource["^/computers$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
                std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::string computerUuid;
            std::smatch match;
            if (std::regex_match(request->query_string, match, pattern))
                computerUuid = match[1];
            else
                computerUuid = request->query_string;

            Computer computer;
            std::vector<NvComputer*> computers = computer.getComputers();

            std::string content;
            try
            {
                nlohmann::json computersJson;
                nlohmann::json computerArray;
                for (int i = 0; i < computers.size(); i++)
                {
                    if (!computerUuid.empty() && computerUuid != computers.at(i)->uuid)
                        continue;

                    computerArray.push_back({
                        {"name", computers.at(i)->name},
                        {"uuid", computers.at(i)->uuid},
                        {"computerState", computer.computerStateToString(computers.at(i)->state)},
                        {"pairState", computer.pairStateToString(computers.at(i)->pairState)},
                        {"wakeable", !computers.at(i)->macAddress.empty()},
                        {"statusUnknown", computers.at(i)->state == NvComputer::CS_UNKNOWN},
                        {"serverSupported", computers.at(i)->isSupportedServerVersion}
                        });

                }

                computersJson["computers"] = computerArray;
                content = computersJson.dump();
            }
            catch (const std::exception& e)
            {
                response->write(SimpleWeb::StatusCode::server_error_internal_server_error, e.what(), headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::server_error_internal_server_error, e.what());
                return;
            }

            response->write(content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, std::to_string(computers.size()));
    };

    m_server.resource["^/apps$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::string computer;
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern))
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            computer = match[1];
            App app;
            app.initialize(computer, true);
            std::vector<NvApp> apps = app.getVisibleApps();

            nlohmann::json appsJson;
            nlohmann::json appArray;
            std::regex hexColorRegex("^#[0-9a-fA-F]{6}$");
            for (const NvApp& nvApp : apps)
            {
                std::string boxArt = nvApp.boxArt.empty()
                    ? app.getAppBoxArt(const_cast<NvApp&>(nvApp))
                    : nvApp.boxArt;

                appArray.push_back({
                    {"name", nvApp.name},
                    {"running", app.getRunningAppId() == nvApp.id},
                    {"boxArtUrl", boxArt},
                    {"hidden", nvApp.hidden},
                    {"id", std::to_string(nvApp.id)},
                    {"directLaunch", nvApp.directLaunch},
                    {"appCollectorGame", nvApp.isAppCollectorGame},
                    {"gamePlatform", nvApp.gamePlatform},
                    {"guid", nvApp.guid},
                    {"lastAppStartTime", nvApp.lastAppStartTime},
                    });
            }
            appsJson["apps"] = appArray;

            std::string content = appsJson.dump();

            response->write(content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, std::to_string(apps.size()));
    };

    m_server.resource["^/hideapp$"]["PUT"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            nlohmann::json content;
            int appID;
            bool hide;
            std::string computerUUID;
            try
            {
                content = nlohmann::json::parse(request->content);

                computerUUID = content["computer"];
                hide = content["hide"];
                std::string app = content["app"];
                appID = std::stoi(app);
            }
            catch (const std::exception& e)
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what(), headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, e.what());
                return;
            }

            App app;
            app.initialize(computerUUID, false);
            app.setAppHidden(appID, hide);

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/razerid/availability$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern))
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string computer = match[1];
            Pair pair;
            std::string errorString;
            bool available = pair.razerIDAvailability(computer, errorString);

            nlohmann::json jsonState;
            jsonState["available"] = available;
            jsonState["message"] = errorString;

            std::string content = jsonState.dump();
            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/pair$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\\&useRazerJWT=(true|false)$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string computerUUID = match[1];
            bool useRazerJWT = match[2] == "true" ? true : false;
            std::string pin = ComputerManager::getInstance()->generatePinString();
            Pair pair(computerUUID, pin, useRazerJWT);

            std::string taskId;
            std::string errorString;
            nlohmann::json jsonPin;
            if(!pair.startPairing(taskId, errorString))
            {
                jsonPin["pin"] = "";
                jsonPin["taskid"] = "";
                jsonPin["msg"] = errorString;
                std::string content = jsonPin.dump();
                response->write(SimpleWeb::StatusCode::client_error_bad_request, content, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, content);
                return;
            }

            jsonPin["pin"] = pin;
            jsonPin["taskid"] = taskId;
            jsonPin["msg"] = errorString;
            std::string content = jsonPin.dump();

            response->write(SimpleWeb::StatusCode::success_accepted, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_accepted, content);
    };

    m_server.resource["^/pairstate$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^taskid=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string taskId = match[1];
            Pair pair;
            AsyncTaskManager::Result result;
            if(!pair.getPairTaskResult(taskId, result))
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            nlohmann::json jsonState;
            jsonState["completed"] = result.isCompleted;
            jsonState["succeed"] = result.isSucceed;
            jsonState["errorstring"] = result.errorString;

            std::string content = jsonState.dump();
            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/cancelpair$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^taskid=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string taskId = match[1];
            Pair pair;
            AsyncTaskManager::Result result;
            if (!pair.cancelPairing(taskId, result))
            {
                response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
                return;
            }

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/stream$"]["GET"] =
       [this](std::shared_ptr<Server::Response> response,
              std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\\&app=(\\d+)$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string computerUUID = match[1];
            int appID = std::stoi(match[2]);

            Stream stream(computerUUID, appID);
            bool success;
            std::string errorString;
            success = stream.startStreaming(errorString);

            nlohmann::json jsonMessage;
            jsonMessage["succeed"] = success;
            jsonMessage["errorstring"] = errorString;
            std::string content = jsonMessage.dump();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/streamstate$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Stream stream;
            AsyncTaskManager::Result result;
            if (!stream.getStreamTaskResult(result))
            {
                response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
                return;
            }

            nlohmann::json jsonState;
            jsonState["completed"] = result.isCompleted;
            jsonState["succeed"] = result.isSucceed;
            jsonState["errorstring"] = result.errorString;
            std::string content = jsonState.dump();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/addcomputer$"]["POST"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::string ip;
            std::string taskId;
            std::string errorString;
            nlohmann::json jsonAdd;
            try
            {
                nlohmann::json body = nlohmann::json::parse(request->content);

                ip = body["ip"];
            }
            catch (const std::exception& e)
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what(), headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, e.what());
                return;
            }

            AddComputer adder(ip);
            if(!adder.startAdding(taskId, errorString))
            {
                jsonAdd["taskid"] = "";
                jsonAdd["msg"] = errorString;
                std::string content = jsonAdd.dump();
                response->write(SimpleWeb::StatusCode::client_error_bad_request, content, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, content);
                return;
            }

            jsonAdd["taskid"] = taskId;
            jsonAdd["msg"] = "";
            std::string content = jsonAdd.dump();

            response->write(SimpleWeb::StatusCode::success_accepted, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_accepted, content);
    };

    m_server.resource["^/addstate$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^taskid=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string taskId = match[1];
            AsyncTaskManager::Result result;
            AddComputer adder;
            if(!adder.getAddTaskResult(taskId, result))
            {
                response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
                return;
            }

            nlohmann::json jsonState;
            jsonState["completed"] = result.isCompleted;
            jsonState["succeed"] = result.isSucceed;
            jsonState["errorstring"] = result.errorString;
            std::string content = jsonState.dump();
            
            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/deletecomputer$"]["DELETE"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern))
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string computerUUID = match[1];
            nlohmann::json jsonDelete;
            std::string taskId;
            std::string errorString;
            DeleteComputer deleter(computerUUID);
            if(!deleter.startDeleting(taskId, errorString))
            {
                jsonDelete["taskid"] = "";
                jsonDelete["msg"] = errorString;
                std::string content = jsonDelete.dump();
                response->write(SimpleWeb::StatusCode::client_error_bad_request, content, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, content);
                return;
            }

            jsonDelete["taskid"] = taskId;
            jsonDelete["msg"] = "";
            std::string content = jsonDelete.dump();

            response->write(SimpleWeb::StatusCode::success_accepted, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_accepted, content);
    };

    m_server.resource["^/deletestate$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^taskid=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string taskId = match[1];
            AsyncTaskManager::Result result;
            DeleteComputer deleter;
            if(!deleter.getDeleteTaskResult(taskId, result))
            {
                response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
                return;
            }

            nlohmann::json jsonState;
            jsonState["completed"] = result.isCompleted;
            jsonState["succeed"] = result.isSucceed;
            jsonState["errorstring"] = result.errorString;
            std::string content = jsonState.dump();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/settings$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Settings settings;
            std::string content = settings.getSettings();

            response->write(content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/settings$"]["PUT"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Settings settings;
            settings.modifySettings(request->content.string());

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/settings/reset$"]["PUT"] =
        [this](std::shared_ptr<Server::Response> response,
           std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Settings settings;
            settings.resetSettings();

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/settings/screeninfo$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Settings settings;
            std::string content = settings.getScreenInfo();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/exit$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::string errorString;
            Quit quit;
            if (!(quit.startExiting(errorString)))
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/something$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            Computer computer;
            std::vector<NvComputer*> computers = computer.getComputers();

            try
            {
                // Remoteplay introduction page display content
                int onlineHostNum = 0;
                std::string firstOnlineName;
                for (const NvComputer* computer : computers)
                {
                    if (NvComputer::CS_ONLINE == computer->state)
                    {
                        if(firstOnlineName.empty())
                            firstOnlineName = computer->name;

                        ++onlineHostNum;
                    }
                }
                nlohmann::json responseJson;
                responseJson["onlineHostNum"] = onlineHostNum;
                responseJson["firstOnlineHost"] = firstOnlineName;

                // Current computer name
                responseJson["currentDeviceName"] = SystemProperties::get()->deviceName;

                // Remoteplay version
                responseJson["version"] = version;

                std::string content = responseJson.dump();
                response->write(SimpleWeb::StatusCode::success_ok, content, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
            }
            catch (const std::exception& e)
            {
                response->write(SimpleWeb::StatusCode::server_error_internal_server_error, e.what(), headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::server_error_internal_server_error, e.what());
                return;
            }
    };

    m_server.resource["^/quitapp$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^computer=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string computerUUID = match[1];
            App app;
            app.initialize(computerUUID, false);
            std::string taskId;
            std::string errorString;
            nlohmann::json responseJson;

            if (!app.startQuiting(taskId, errorString))
            {
                responseJson["taskid"] = "";
                responseJson["msg"] = errorString;
                std::string content = responseJson.dump();
                response->write(SimpleWeb::StatusCode::client_error_bad_request, content, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, content);
                return;
            }

            responseJson["taskid"] = taskId;
            responseJson["msg"] = "";
            std::string content = responseJson.dump();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    m_server.resource["^/quitstate$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            std::regex pattern("^taskid=([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$");
            std::smatch match;
            if (!std::regex_match(request->query_string, match, pattern)) {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request);
                return;
            }

            std::string taskId = match[1];
            AsyncTaskManager::Result result;
            App app;
            
            if (!app.getQuitTaskResult(taskId, result))
            {
                response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
                return;
            }

            nlohmann::json responseJson;
            responseJson["completed"] = result.isCompleted;
            responseJson["succeed"] = result.isSucceed;
            responseJson["errorstring"] = result.errorString;
            std::string content = responseJson.dump();

            response->write(SimpleWeb::StatusCode::success_ok, content, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok, content);
    };

    // Default GET-example. If no other matches, this anonymous function will be called.
    // Will respond with content in the web/-directory, and its subdirectories.
    // Default file: index.html
    // Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    m_server.default_resource["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            response->write(SimpleWeb::StatusCode::client_error_not_found, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_not_found);
    };

    m_server.resource["^/XRazerJWT$"]["POST"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            try
            {
                nlohmann::json content = nlohmann::json::parse(request->content);
                Razer::getInstance()->setpairToken(content["RazerPairToken"]);
                Razer::getInstance()->setUUID(content["RazerUUID"]);
            }
            catch (const std::exception& e)
            {
                response->write(SimpleWeb::StatusCode::client_error_bad_request, e.what(), headers);
                m_reqLogger->logRequest(request, SimpleWeb::StatusCode::client_error_bad_request, e.what());
                return;
            }

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.resource["^/alive$"]["GET"] =
        [this](std::shared_ptr<Server::Response> response,
            std::shared_ptr<Server::Request> request)
    {
            SimpleWeb::CaseInsensitiveMultimap headers;
            headers.emplace("Access-Control-Allow-Origin", ACCESS_CONTROL_ALLOW_ORIGIN);
            headers.emplace("Access-Control-Allow-Methods", ACCESS_CONTROL_ALLOW_METHODS);
            headers.emplace("Access-Control-Allow-Headers", ACCESS_CONTROL_ALLOW_HEADERS);

            response->write(SimpleWeb::StatusCode::success_ok, headers);
            m_reqLogger->logRequest(request, SimpleWeb::StatusCode::success_ok);
    };

    m_server.on_error =
        [](std::shared_ptr<Server::Request> request,
           const SimpleWeb::error_code &)
    {
        // Handle errors here
        // Note that connection timeouts will also call this handle with ec set to SimpleWeb::errc::operation_canceled
    };

    std::promise<unsigned short> server_port;
    m_server_thread = std::thread([this, &server_port]() {
        // Start server
        this->m_server.start([&server_port](unsigned short port) {
            server_port.set_value(port);
        });
    });

    LOG(INFO) << "Server listening on port " << server_port.get_future().get();
}

HTTPServer::~HTTPServer()
{
    m_server.stop();
    m_server_thread.join();

    delete m_reqLogger;
}

