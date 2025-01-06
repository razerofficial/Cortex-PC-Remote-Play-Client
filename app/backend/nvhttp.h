#pragma once

#include "nvapp.h"
#include "nvaddress.h"

#include <vector>
#include <Limelight.h>



class RazerUrl
{
public:
    RazerUrl()
        : m_scheme("")
        , m_host("")
        , m_port(0)
        , m_path("")
        , m_query("")
    {}

    void setScheme(const std::string& scheme)
    {
        m_scheme = scheme + "://";
    }

    std::string getScheme()
    {
        return m_scheme;
    }

    void setHost(const std::string& host)
    {
        m_host = host;
    }

    void setPort(unsigned short port)
    {
        m_port = port;
    }

    void setPath(const std::string& path)
    {
        m_path = path;
    }

    void setQuery(const std::string& query)
    {
        m_query = '?' + query;
    }

    unsigned short port()
    {
        return m_port;
    }

    std::string getIPAndPort()
    {
        return m_host + ':' + std::to_string(m_port);
    }

    std::string getPath()
    {
        return m_path + m_query;
    }

    std::string toString()
    {
        return m_scheme + m_host + ':' + std::to_string(m_port) + m_path + m_query;
    }

private:
    std::string m_scheme;
    std::string m_host;
    unsigned short m_port;
    std::string m_path;
    std::string m_query;
};

class NvComputer;

class NvDisplayMode
{
public:
    bool operator==(const NvDisplayMode& other) const
    {
        return width == other.width &&
                height == other.height &&
                refreshRate == other.refreshRate;
    }

    int width;
    int height;
    int refreshRate;
};

class GfeHttpResponseException : public std::exception
{
public:
    GfeHttpResponseException(int statusCode, std::string message) :
        m_StatusCode(statusCode),
        m_StatusMessage(message)
    {

    }

    const char* what() const throw()
    {
        return m_StatusMessage.c_str();
    }

    const char* getStatusMessage() const
    {
        return m_StatusMessage.c_str();
    }

    int getStatusCode() const
    {
        return m_StatusCode;
    }

    std::string toString() const
    {
        return m_StatusMessage + " (Error " + std::to_string(m_StatusCode) + ")";
    }

private:
    int m_StatusCode;
    std::string m_StatusMessage;
};

class NetworkReplyException : public std::exception
{
public:
    NetworkReplyException(int error, std::string errorText) :
        m_errorCode(error),
        m_errorText(errorText)
    {

    }

    const char* what() const throw()
    {
        return m_errorText.c_str();
    }

    const char* getErrorText() const
    {
        return m_errorText.c_str();
    }

    int getError() const
    {
        return m_errorCode;
    }

    std::string toString() const
    {
        return m_errorText + " (Error " + std::to_string(m_errorCode) + ")";
    }

private:
    int m_errorCode;
    std::string m_errorText;
};

class NvHTTP
{
public:
    enum NvLogLevel {
        NVLL_NONE,
        NVLL_ERROR,
        NVLL_VERBOSE
    };

    explicit NvHTTP(NvAddress address, uint16_t httpsPort, std::string serverCert);

    explicit NvHTTP(NvComputer* computer);

    static
    int
    getCurrentGame(std::string serverInfo);

    std::string
    getServerInfo(NvLogLevel logLevel, bool fastFail = false);

    static
    void
    verifyResponseStatus(std::string xml);

    static
    std::string
    getXmlString(std::string xml,
                  std::string tagName);

    static
    std::string
    getXmlStringFromHex(std::string xml,
                         std::string tagName);

    std::string
    openConnectionToString(RazerUrl baseUrl,
                            std::string command,
                            std::string arguments,
                            int timeoutMs);

    void stopConnection();

    void setServerCert(std::string serverCert);

    void setAddress(NvAddress address);
    void setHttpsPort(uint16_t port);

    NvAddress address();

    std::string serverCert();

    uint16_t httpPort();

    uint16_t httpsPort();

    static
    std::vector<int>
    parseQuad(std::string quad);

    void
    quitApp();

    void
    startApp(std::string verb,
             bool isGfe,
             int appId,
             PSTREAM_CONFIGURATION streamConfig,
             bool sops,
             bool localAudio,
             int gamepadMask,
             bool persistGameControllersOnDisconnect,
             std::string& rtspSessionUrl);

    std::vector<NvApp>
    getAppList();

    std::string
    getBoxArt(int appId);

    static
    std::vector<NvDisplayMode>
    getDisplayModeList(std::string serverInfo);

    RazerUrl m_BaseUrlHttp;
    RazerUrl m_BaseUrlHttps;
private:

    std::string
    openConnection(RazerUrl baseUrl,
                    std::string command,
                    std::string arguments,
                    int timeoutMs);

    std::atomic<bool> m_stop;
    NvAddress m_Address;
    std::string m_ServerCert;
};
