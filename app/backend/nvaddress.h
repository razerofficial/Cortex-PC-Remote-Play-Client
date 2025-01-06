#pragma once

#include <string>

#define DEFAULT_HTTP_PORT 51337
#define DEFAULT_HTTPS_PORT 51332

class NvAddress
{
public:
    NvAddress();
    explicit NvAddress(std::string addr, uint16_t port);

    uint16_t port() const;
    void setPort(uint16_t port);

    std::string address() const;
    void setAddress(std::string addr);

    bool isNull() const;
    std::string toString() const;

    bool operator==(const NvAddress& other) const
    {
        return m_Address == other.m_Address &&
                m_Port == other.m_Port;
    }

    bool operator!=(const NvAddress& other) const
    {
        return !operator==(other);
    }

private:
    std::string m_Address;
    uint16_t m_Port;
};
