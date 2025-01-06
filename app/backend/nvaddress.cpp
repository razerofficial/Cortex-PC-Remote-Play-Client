#include "nvaddress.h"

#include <sstream>

NvAddress::NvAddress()
{
    setAddress("");
    setPort(0);
}

NvAddress::NvAddress(std::string addr, uint16_t port)
{
    setAddress(addr);
    setPort(port);
}

uint16_t NvAddress::port() const
{
    return m_Port;
}

std::string NvAddress::address() const
{
    return m_Address;
}

void NvAddress::setPort(uint16_t port)
{
    m_Port = port;
}

void NvAddress::setAddress(std::string addr)
{
    m_Address = addr;
}

bool NvAddress::isNull() const
{
    return m_Address.empty();
}

std::string NvAddress::toString() const
{
    if (m_Address.empty()) {
        return "<NULL>";
    }

    std::stringstream ss;
    ss<<m_Address<<':'<<m_Port;
    return ss.str();
}
