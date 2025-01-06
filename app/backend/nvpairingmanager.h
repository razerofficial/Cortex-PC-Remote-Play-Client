#pragma once

#include "identitymanager.h"
#include "nvhttp.h"

#include <openssl/x509.h>
#include <openssl/evp.h>

class NvPairingManager
{
public:
    enum PairState
    {
        PAIRED,
        PIN_WRONG,
        FAILED,
        ALREADY_IN_PROGRESS,
        RAZER_WRONG
    };

    explicit NvPairingManager(NvComputer* computer);

    ~NvPairingManager();

    PairState
    pair(std::string appVersion, std::string pin, bool useRazerJWT, std::string& serverCert);

    /*
     * 配对的第一个阶段getservercert，是一个不超时的阻塞请求。
     * 如果程序退出时，执行配对的线程一直阻塞在第一个配对阶段，
     * 会造成该线程不会被正常关闭。
    */
    void cancelGetservercert();

private:
    std::string
    generateRandomBytes(int length);

    std::string
    saltPin(const std::string& salt, std::string pin);

    std::string
    encrypt(const std::string& plaintext, const std::string& key);

    std::string
    decrypt(const std::string& ciphertext, const std::string& key);

    std::string
    getSignatureFromPemCert(const std::string& certificate);

    bool
    verifySignature(const std::string& data, const std::string& signature, const std::string& serverCertificate);

    std::string
    signMessage(const std::string& message);

    NvHTTP m_Http;
    X509* m_Cert;
    EVP_PKEY* m_PrivateKey;
};
