#include "razer.h"

#include <json.hpp>
#include <glog/logging.h>
#include <Simple-Web-Server/client_https.hpp>

std::map<std::string, std::string> Razer::textMap = {
    {"The specified host does not exist!", "remote_play_client_host_not_exist"},
    {"The game wasn't started on this PC. You must quit the game on the host PC manually or use the device that originally started the game.", "remote_play_client_quit_res_failed_1"},
    {"Network error.", "remote_play_client_quit_res_failed_2"},
    {"All sessions must be disconnected before quitting. Pls reconnect the Host or make sure this is the only connection on this Host.", "remote_play_host_quit_failed_1"},
    //{"The specified host does not exist!", "remote_play_client_host_not_exist"},
    {"The paired Razer ID does not match!", "remote_play_client_razer_pair_msg_2"},
    {"Razer ID pairing has been disabled by the host PC!", "remote_play_client_razer_pair_msg_3"},
    {"Failed to obtain host Razer ID pairing mode!", "remote_play_client_razer_pair_msg_4"},
    //{"The specified host does not exist!", "remote_play_client_host_not_exist"},
    {"The specified host is offline!", "remote_play_client_pair_failed_2"},
    {"The specified host is already paired!", "remote_play_client_pair_failed_3"},
    {"The PIN from the host PC didn't match. Please try again.", "remote_play_client_pair_res_failed_1"},
    {"You cannot pair while a previous session is still running on the host PC. Quit any running games or reboot the host PC, then try pairing again.", "remote_play_client_pair_res_failed_2"},
    {"Pairing failed. Please try again.", "remote_play_client_pair_res_failed_3"},
    {"Another pairing attempt is already in progress.", "remote_play_client_pair_res_failed_4"},
    {"Pairing failed using Razer ID. Please check the Razer ID and try again", "remote_play_client_pair_res_failed_5"},
    {"Encountered an error while connecting to the host PC. Please try again.", "remote_play_client_pair_res_failed_6"},
    {"PIN code expired.", "remote_play_client_pair_res_failed_7"},


    {"Remote Play is currently streaming.", "remote_play_client_stream_failed_1"},
    {"The specified host PC does not exist!", "remote_play_client_stream_failed_2"},
    {"The specified host PC is offline!", "remote_play_client_stream_failed_3"},
    {"The specified host PC is not paired!", "remote_play_client_stream_failed_4"},
    {"The specified application does not exist!", "remote_play_client_stream_failed_5"},
    {"There is currently an application that has not been exited. Please exit the application first.", "remote_play_client_stream_failed_6"},
    {"The specified streaming host is exiting the game.", "remote_play_client_stream_failed_7"},


    {"Error initializing session.", "remote_play_client_stream_res_failed_1"},
    {"Error connecting to host.", "remote_play_client_stream_res_failed_2"},
    {"Internal error.", "remote_play_client_stream_res_failed_3"},
    {"Error resetting decoder.", "remote_play_client_stream_res_failed_4"},
    {"Connection terminated.", "remote_play_client_stream_res_failed_5"},

    {"Could not connect to the Host.", "remote_play_client_add_failed_1"},

    //{"The specified host does not exist!", "remote_play_client_host_not_exist"},
};

using HttpsClient = SimpleWeb::Client<SimpleWeb::HTTPS>;

Razer* Razer::getInstance()
{
	static Razer s_instance;
	return &s_instance;
}

void Razer::setpairToken(const std::string& token)
{
	std::lock_guard locker(m_mtx);
	m_pairToken = token;
	LOG(INFO) << "token: " << m_pairToken;
}

void Razer::setUUID(const std::string& uuid)
{
	std::lock_guard locker(m_mtx);
	m_uuid = uuid;
	LOG(INFO) << "uuid: " << m_uuid;
}

std::string Razer::getpairToken()
{
	return m_pairToken;
}

std::string Razer::getUUID()
{
	return m_uuid;
}

std::string Razer::getRazerSecret(const std::string& RazerJWT)
{
    std::string secret;

    HttpsClient client("nexus-prod.mobile.razer.com", false);

    SimpleWeb::CaseInsensitiveMultimap headers;
    headers.emplace("X-Razer-JWT", RazerJWT);
    headers.emplace("Accept", "application/json");

    try {
        auto response = client.request("POST", "/neuron/api/secret/generate", "", headers);

        if (response->status_code != "200 OK") {
            LOG(ERROR) << "Failed to generate razer secret! HTTP status: " << response->status_code << std::endl;
        }
        else {
            secret = response->content.string();
        }
    }
    catch (const std::exception& e) {
        LOG(ERROR) << "Failed to generate razer secret! Exception: " << e.what() << std::endl;
    }

    return secret;
}

std::string Razer::getJsonString(const std::string& json, const std::string& tagName)
{
    std::string tagValue;
    try
    {
        nlohmann::json jsonObj = nlohmann::json::parse(json);
        tagValue = jsonObj.at(tagName);
    }
    catch (const std::exception&)
    {
    }

    return tagValue;
}