#pragma once

#include <string>
class Configure;

class IdentityManager
{
public:
    std::string
    getUniqueId();

    std::string
    getCertificate();

    std::string
    getPrivateKey();

    static
    IdentityManager*
    get();

private:
    IdentityManager();

    void
    createCredentials(Configure* settings);

    std::string m_CachedPrivateKey;
    std::string m_CachedPemCert;

    std::string m_CachedUniqueId;

    static IdentityManager* s_Im;
};
