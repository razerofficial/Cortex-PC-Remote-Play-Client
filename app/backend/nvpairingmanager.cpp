#include "nvpairingmanager.h"
#include "utils.h"
#include "razer.h"
#include "systemproperties.h"

#include <stdexcept>

#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <openssl/aes.h>

#include <glog/logging.h>

#define REQUEST_TIMEOUT_MS 5000
#define RAZER_ID_PAIR_TIMEOUT_MS 60000


NvPairingManager::NvPairingManager(NvComputer* computer) :
    m_Http(computer)
{
    std::string cert = IdentityManager::get()->getCertificate();
    BIO *bio = BIO_new_mem_buf(cert.data(), -1);
    THROW_BAD_ALLOC_IF_NULL(bio);

    m_Cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (m_Cert == nullptr)
    {
        throw std::runtime_error("Unable to load certificate");
    }

    std::string pk = IdentityManager::get()->getPrivateKey();
    bio = BIO_new_mem_buf(pk.data(), -1);
    THROW_BAD_ALLOC_IF_NULL(bio);

    m_PrivateKey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);
    if (m_PrivateKey == nullptr)
    {
        throw std::runtime_error("Unable to load private key");
    }
}

NvPairingManager::~NvPairingManager()
{
    X509_free(m_Cert);
    EVP_PKEY_free(m_PrivateKey);
}

std::string
NvPairingManager::generateRandomBytes(int length)
{
    char* data = static_cast<char*>(alloca(length));
    RAND_bytes(reinterpret_cast<unsigned char*>(data), length);
    return std::string(data, length);
}

std::string
NvPairingManager::encrypt(const std::string& plaintext, const std::string& key)
{
    std::string ciphertext(plaintext.size(), 0);
    EVP_CIPHER_CTX* cipher;
    int ciphertextLen;

    cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    EVP_EncryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char*>(key.data()), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    EVP_EncryptUpdate(cipher,
                      reinterpret_cast<unsigned char*>(const_cast<char*>(ciphertext.data())),
                      &ciphertextLen,
                      reinterpret_cast<const unsigned char*>(plaintext.data()),
                      plaintext.length());
    assert(ciphertextLen == ciphertext.length());

    EVP_CIPHER_CTX_free(cipher);

    return ciphertext;
}

std::string
NvPairingManager::decrypt(const std::string& ciphertext, const std::string& key)
{
    std::string plaintext(ciphertext.size(), 0);
    EVP_CIPHER_CTX* cipher;
    int plaintextLen;

    cipher = EVP_CIPHER_CTX_new();
    THROW_BAD_ALLOC_IF_NULL(cipher);

    EVP_DecryptInit(cipher, EVP_aes_128_ecb(), reinterpret_cast<const unsigned char*>(key.data()), NULL);
    EVP_CIPHER_CTX_set_padding(cipher, 0);

    EVP_DecryptUpdate(cipher,
                      reinterpret_cast<unsigned char*>(const_cast<char*>(plaintext.data())),
                      &plaintextLen,
                      reinterpret_cast<const unsigned char*>(ciphertext.data()),
                      ciphertext.length());
    assert(plaintextLen == plaintext.length());

    EVP_CIPHER_CTX_free(cipher);

    return plaintext;
}

std::string
NvPairingManager::getSignatureFromPemCert(const std::string& certificate)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    BIO* bio = BIO_new_mem_buf(const_cast<char*>(certificate.data()), -1);
#else
    BIO* bio = BIO_new_mem_buf(certificate.data(), -1);
#endif
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);

#if (OPENSSL_VERSION_NUMBER < 0x10002000L)
    ASN1_BIT_STRING *asnSignature = cert->signature;
#elif (OPENSSL_VERSION_NUMBER < 0x10100000L)
    ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#else
    const ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, cert);
#endif

    std::string signature(reinterpret_cast<char*>(asnSignature->data), asnSignature->length);

    X509_free(cert);

    return signature;
}

bool
NvPairingManager::verifySignature(const std::string& data, const std::string& signature, const std::string& serverCertificate)
{
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
    BIO* bio = BIO_new_mem_buf(const_cast<char*>(serverCertificate.data()), -1);
#else
    BIO* bio = BIO_new_mem_buf(serverCertificate.data(), -1);
#endif
    THROW_BAD_ALLOC_IF_NULL(bio);

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free_all(bio);

    EVP_PKEY* pubKey = X509_get_pubkey(cert);
    THROW_BAD_ALLOC_IF_NULL(pubKey);

    EVP_MD_CTX* mdctx = EVP_MD_CTX_create();
    THROW_BAD_ALLOC_IF_NULL(mdctx);

    EVP_DigestVerifyInit(mdctx, nullptr, EVP_sha256(), nullptr, pubKey);
    EVP_DigestVerifyUpdate(mdctx, data.data(), data.length());
    int result = EVP_DigestVerifyFinal(mdctx, reinterpret_cast<unsigned char*>(const_cast<char*>(signature.data())), signature.length());

    EVP_PKEY_free(pubKey);
    EVP_MD_CTX_destroy(mdctx);
    X509_free(cert);

    return result > 0;
}

std::string
NvPairingManager::signMessage(const std::string& message)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_create();
    THROW_BAD_ALLOC_IF_NULL(ctx);

    EVP_DigestSignInit(ctx, NULL, EVP_sha256(), NULL, m_PrivateKey);
    EVP_DigestSignUpdate(ctx, reinterpret_cast<unsigned char*>(const_cast<char*>(message.data())), message.length());

    size_t signatureLength = 0;
    EVP_DigestSignFinal(ctx, NULL, &signatureLength);

    std::string signature((int)signatureLength, 0);
    EVP_DigestSignFinal(ctx, reinterpret_cast<unsigned char*>(const_cast<char*>(signature.data())), &signatureLength);

    EVP_MD_CTX_destroy(ctx);

    return signature;
}

std::string
NvPairingManager::saltPin(const std::string& salt, std::string pin)
{
    return salt + pin;
}

NvPairingManager::PairState
NvPairingManager::pair(std::string appVersion, std::string pin, bool useRazerJWT, std::string& serverCert)
{
    std::string razerPairArgs;
    if (useRazerJWT)
    {
        std::string pairToken = Razer::getInstance()->getpairToken();
        std::string razerUUID = Razer::getInstance()->getUUID();
        if (pairToken.empty() || razerUUID.empty())
        {
            LOG(ERROR) << "Failed pairing: razerPairToken or razerUUID is empty!";
            return PairState::RAZER_WRONG;
        }

        //FIX ME: some time get razerSercet is empty.
        std::string razerSercet = Razer::getRazerSecret(pairToken);
        if (razerSercet.empty())
        {
            LOG(ERROR) << "Failed pairing: razerSercet is empty!";
            return PairState::RAZER_WRONG;
        }

        std::string secret = Razer::getJsonString(razerSercet, "secret");
        std::string razerPincodeUuid = Razer::getJsonString(razerSercet, "uuid");
        if (secret.empty() || razerPincodeUuid.empty())
        {
            LOG(ERROR) << "Failed pairing: secret or razerPincodeUuid is empty!";
            return PairState::RAZER_WRONG;
        }

        std::string razerAesKey = Crypt::calculateMD5(secret);
        //FIX ME: server未按照文档约定实现,对razerAesKey再次hash。修复流程后直接删除下行即可！
        razerAesKey = Crypt::calculateSHA256(razerAesKey);

        std::string paddedPIN = Crypt::pkcs7Pad(pin, AES_BLOCK_SIZE);
        std::string cipherPIN = encrypt(paddedPIN, razerAesKey);
        std::string razerPincode = StringUtils::stringToHex(cipherPIN);

        razerPairArgs = "&razer_pincode=" + razerPincode + "&razer_pincode_uuid=" + razerPincodeUuid + "&razer_uuid=" + razerUUID + "&env=prod";
    }

    int serverMajorVersion = NvHTTP::parseQuad(appVersion).at(0);
    LOG(INFO) << "Pairing with server generation:" << serverMajorVersion;

    std::function<std::string(const std::string&)> hash;
    int hashLength;
    if (serverMajorVersion >= 7)
    {
        // Gen 7+ uses SHA-256 hashing
        hash = Crypt::calculateSHA256;
        hashLength = 32;
    }
    else
    {
        // Prior to Gen 7 uses SHA-1 hashing
        hash = Crypt::calculateSHA1;
        hashLength = 20;
    }

    std::string salt = generateRandomBytes(16);
    std::string saltedPin = saltPin(salt, pin);

    std::string aesKey = hash(saltedPin);
    aesKey = aesKey.substr(0, 16);

    std::string getCert = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                    "pair",
                                                    "devicename=roth&updateState=1&phrase=getservercert&devicenickname=" + SystemProperties::get()->deviceName +
                                                    "&salt=" + StringUtils::stringToHex(salt) + "&clientcert=" + StringUtils::stringToHex(IdentityManager::get()->getCertificate()) +
                                                    (useRazerJWT ? razerPairArgs : ""),
                                                    useRazerJWT ? RAZER_ID_PAIR_TIMEOUT_MS : 0);
    NvHTTP::verifyResponseStatus(getCert);
    if (NvHTTP::getXmlString(getCert, "paired") != "1")
    {
        LOG(ERROR) << "Failed pairing at stage #1";
        return PairState::FAILED;
    }

    std::string serverCertStr = NvHTTP::getXmlStringFromHex(getCert, "plaincert");
    if (serverCertStr.empty())
    {
        LOG(ERROR) << "Server likely already pairing";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::ALREADY_IN_PROGRESS;
    }

    // Pin this cert for TLS until pairing is complete. If successful, we will propagate
    // the cert into the NvComputer object and persist it.
    m_Http.setServerCert(serverCertStr);

    std::string randomChallenge = generateRandomBytes(16);
    std::string encryptedChallenge = encrypt(randomChallenge, aesKey);
    std::string challengeXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                         "pair",
                                                         "devicename=roth&updateState=1&clientchallenge=" +
                                                         StringUtils::stringToHex(encryptedChallenge),
                                                         REQUEST_TIMEOUT_MS);
    NvHTTP::verifyResponseStatus(challengeXml);
    if (NvHTTP::getXmlString(challengeXml, "paired") != "1")
    {
        LOG(ERROR) << "Failed pairing at stage #2";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::FAILED;
    }

    std::string challengeResponseData = decrypt(m_Http.getXmlStringFromHex(challengeXml, "challengeresponse"), aesKey);
    std::string clientSecretData = generateRandomBytes(16);
    std::string challengeResponse;
    std::string serverResponse(challengeResponseData.data(), hashLength);

#if (OPENSSL_VERSION_NUMBER < 0x10002000L)
    ASN1_BIT_STRING *asnSignature = m_Cert->signature;
#elif (OPENSSL_VERSION_NUMBER < 0x10100000L)
    ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, m_Cert);
#else
    const ASN1_BIT_STRING *asnSignature;
    X509_get0_signature(&asnSignature, NULL, m_Cert);
#endif

    challengeResponse.append(challengeResponseData.data() + hashLength, 16);
    challengeResponse.append(reinterpret_cast<char*>(asnSignature->data), asnSignature->length);
    challengeResponse.append(clientSecretData);

    std::string paddedHash = hash(challengeResponse);
    paddedHash.resize(32);
    std::string encryptedChallengeResponseHash = encrypt(paddedHash, aesKey);
    std::string respXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                    "pair",
                                                    "devicename=roth&updateState=1&serverchallengeresp=" +
                                                    StringUtils::stringToHex(encryptedChallengeResponseHash),
                                                    REQUEST_TIMEOUT_MS);
    NvHTTP::verifyResponseStatus(respXml);
    if (NvHTTP::getXmlString(respXml, "paired") != "1")
    {
        LOG(ERROR) << "Failed pairing at stage #3";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::FAILED;
    }

    std::string pairingSecret = NvHTTP::getXmlStringFromHex(respXml, "pairingsecret");
    std::string serverSecret = pairingSecret.substr(0, 16);
    std::string serverSignature = pairingSecret.substr(16);

    if (!verifySignature(serverSecret,
                         serverSignature,
                         serverCertStr))
    {
        LOG(ERROR) << "MITM detected";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::FAILED;
    }

    std::string expectedResponseData;
    expectedResponseData.append(randomChallenge);
    expectedResponseData.append(getSignatureFromPemCert(serverCertStr));
    expectedResponseData.append(serverSecret);
    if (hash(expectedResponseData) != serverResponse)
    {
        LOG(ERROR) << "Incorrect PIN";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::PIN_WRONG;
    }

    std::string clientPairingSecret;
    clientPairingSecret.append(clientSecretData);
    clientPairingSecret.append(signMessage(clientSecretData));

    std::string secretRespXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttp,
                                                          "pair",
                                                          "devicename=roth&updateState=1&clientpairingsecret=" +
                                                          StringUtils::stringToHex(clientPairingSecret),
                                                          REQUEST_TIMEOUT_MS);
    NvHTTP::verifyResponseStatus(secretRespXml);
    if (NvHTTP::getXmlString(secretRespXml, "paired") != "1")
    {
        LOG(ERROR) << "Failed pairing at stage #4: " << secretRespXml;
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::FAILED;
    }

    std::string pairChallengeXml = m_Http.openConnectionToString(m_Http.m_BaseUrlHttps,
                                                             "pair",
                                                             "devicename=roth&updateState=1&phrase=pairchallenge",
                                                             REQUEST_TIMEOUT_MS);
    NvHTTP::verifyResponseStatus(pairChallengeXml);
    if (NvHTTP::getXmlString(pairChallengeXml, "paired") != "1")
    {
        LOG(ERROR) << "Failed pairing at stage #5";
        m_Http.openConnectionToString(m_Http.m_BaseUrlHttp, "unpair", "", REQUEST_TIMEOUT_MS);
        return PairState::FAILED;
    }

    serverCert = std::move(serverCertStr);
    return PairState::PAIRED;
}

void NvPairingManager::cancelGetservercert()
{
    m_Http.stopConnection();
}

