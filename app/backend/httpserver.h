#pragma once

#include "Simple-Web-Server/server_http.hpp"
using Server = SimpleWeb::Server<SimpleWeb::HTTP>;

class RequestLogger;

class HTTPServer
{
public:
    HTTPServer();
    ~HTTPServer();

private:
    Server m_server;
    std::thread m_server_thread;
    RequestLogger* m_reqLogger;
};
