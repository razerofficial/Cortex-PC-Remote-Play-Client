#include "identitymanager.h"
#include "utils.h"
#include "settings/configuer.h"
#include <glog/logging.h>

#include <sstream>

#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/rand.h>

#define SER_UNIQUEID "uniqueid"
#define SER_CERT "certificate"
#define SER_KEY "key"

IdentityManager* IdentityManager::s_Im = nullptr;

IdentityManager*
IdentityManager::get()
{
    // This will always be called first on the main thread,
    // so it's safe to initialize without locks.
    if (s_Im == nullptr) {
        s_Im = new IdentityManager();
    }

    return s_Im;
}

void IdentityManager::createCredentials(Configure* settings)
{
    X509* cert = X509_new();
    THROW_BAD_ALLOC_IF_NULL(cert);

    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    THROW_BAD_ALLOC_IF_NULL(ctx);

    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);

    // pk must be initialized on input
    EVP_PKEY* pk = NULL;
    EVP_PKEY_keygen(ctx, &pk);

    EVP_PKEY_CTX_free(ctx);
    THROW_BAD_ALLOC_IF_NULL(pk);

    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 0);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 60 * 60 * 24 * 365 * 20); // 20 yrs
#else
    ASN1_TIME* before = ASN1_STRING_dup(X509_get0_notBefore(cert));
    THROW_BAD_ALLOC_IF_NULL(before);
    ASN1_TIME* after = ASN1_STRING_dup(X509_get0_notAfter(cert));
    THROW_BAD_ALLOC_IF_NULL(after);

    X509_gmtime_adj(before, 0);
    X509_gmtime_adj(after, 60 * 60 * 24 * 365 * 20); // 20 yrs

    X509_set1_notBefore(cert, before);
    X509_set1_notAfter(cert, after);

    ASN1_STRING_free(before);
    ASN1_STRING_free(after);
#endif

    X509_set_pubkey(cert, pk);

    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<unsigned char *>(const_cast<char*>("NVIDIA GameStream Client")),
                               -1, -1, 0);
    X509_set_issuer_name(cert, name);

    X509_sign(cert, pk, EVP_sha256());

    BIO* biokey = BIO_new(BIO_s_mem());
    THROW_BAD_ALLOC_IF_NULL(biokey);
    PEM_write_bio_PrivateKey(biokey, pk, NULL, NULL, 0, NULL, NULL);

    BIO* biocert = BIO_new(BIO_s_mem());
    THROW_BAD_ALLOC_IF_NULL(biocert);
    PEM_write_bio_X509(biocert, cert);

    BUF_MEM* mem;
    BIO_get_mem_ptr(biokey, &mem);
    m_CachedPrivateKey = std::string(mem->data, (int)mem->length);

    BIO_get_mem_ptr(biocert, &mem);
    m_CachedPemCert = std::string(mem->data, (int)mem->length);

    X509_free(cert);
    EVP_PKEY_free(pk);
    BIO_free(biokey);
    BIO_free(biocert);

    settings->setGeneral(SER_CERT, StringUtils::replacePlaceholder(m_CachedPemCert, "\n", "$CR$"));
    settings->setGeneral(SER_KEY, StringUtils::replacePlaceholder(m_CachedPrivateKey, "\n", "$CR$"));
    settings->saveGeneral();

    LOG(INFO) << "Wrote new identity credentials to settings";
}

IdentityManager::IdentityManager()
{
    Configure* settings = Configure::getInstance();

    m_CachedPemCert = settings->getGeneral<std::string>(SER_CERT);
    m_CachedPrivateKey = settings->getGeneral<std::string>(SER_KEY);

    m_CachedPemCert = StringUtils::replacePlaceholder(m_CachedPemCert, "$CR$", "\n");
    m_CachedPrivateKey = StringUtils::replacePlaceholder(m_CachedPrivateKey, "$CR$", "\n");

    if(m_CachedPemCert.empty() || m_CachedPrivateKey.empty()){
        LOG(INFO) << "No existing credentials found";
        createCredentials(settings);
    }

    // We should have valid credentials now. If not, we're screwed
    if (m_CachedPemCert.empty()) {
        LOG(FATAL) << "Newly generated certificate is unreadable";
    }
    if (m_CachedPrivateKey.empty()) {
        LOG(FATAL) << "Newly generated private key is unreadable";
    }
}

std::string
IdentityManager::getUniqueId()
{
    return m_CachedUniqueId;
}

std::string
IdentityManager::getCertificate()
{
    return m_CachedPemCert;
}

std::string
IdentityManager::getPrivateKey()
{
    return m_CachedPrivateKey;
}
