#pragma once

#include <string>
#include <mutex>
#include <map>

class Razer
{
public:
    static Razer* getInstance();

    void setpairToken(const std::string& token);
    void setUUID(const std::string& uuid);

    std::string getpairToken();
    std::string getUUID();

    static std::string getRazerSecret(const std::string& RazerJWT);
    static std::string getJsonString(const std::string& json, const std::string& tagName);

    static std::map<std::string, std::string> textMap;

private:
    Razer() = default;
    Razer(const Razer&) = delete;
    Razer& operator=(const Razer&) = delete;
    ~Razer() = default;

private:
    std::mutex m_mtx;
    std::string m_pairToken;
    std::string m_uuid;
};
