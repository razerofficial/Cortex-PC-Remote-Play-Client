#include "utils.h"

#include <SDL.h>

#include <iostream>
#include <string>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <regex>
#include <chrono>
#include <boost/algorithm/hex.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/beast/core/detail/base64.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAS_X11
#include <X11/Xlib.h>
#endif

#ifdef HAS_WAYLAND
#include <wayland-client.h>
#endif

#ifdef HAVE_DRM
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif

#define VALUE_SET 0x01
#define VALUE_TRUE 0x02

bool WMUtils::isRunningX11()
{
#ifdef HAS_X11
    static SDL_atomic_t isRunningOnX11;

    // If the value is not set yet, populate it now.
    int val = SDL_AtomicGet(&isRunningOnX11);
    if (!(val & VALUE_SET)) {
        Display* display = XOpenDisplay(nullptr);
        if (display != nullptr) {
            XCloseDisplay(display);
        }

        // Populate the value to return and have for next time.
        // This can race with another thread populating the same data,
        // but that's no big deal.
        val = VALUE_SET | ((display != nullptr) ? VALUE_TRUE : 0);
        SDL_AtomicSet(&isRunningOnX11, val);
    }

    return !!(val & VALUE_TRUE);
#endif

    return false;
}

bool WMUtils::isRunningWayland()
{
#ifdef HAS_WAYLAND
    static SDL_atomic_t isRunningOnWayland;

    // If the value is not set yet, populate it now.
    int val = SDL_AtomicGet(&isRunningOnWayland);
    if (!(val & VALUE_SET)) {
        struct wl_display* display = wl_display_connect(nullptr);
        if (display != nullptr) {
            wl_display_disconnect(display);
        }

        // Populate the value to return and have for next time.
        // This can race with another thread populating the same data,
        // but that's no big deal.
        val = VALUE_SET | ((display != nullptr) ? VALUE_TRUE : 0);
        SDL_AtomicSet(&isRunningOnWayland, val);
    }

    return !!(val & VALUE_TRUE);
#endif

    return false;
}

bool WMUtils::isRunningWindowManager()
{
// #if defined(Q_OS_WIN) || defined(Q_OS_DARWIN)
//     // Windows and macOS are always running a window manager
//     return true;
#if defined(_WIN32) || defined(_WIN64) || defined(__APPLE__) && defined(__MACH__)
    return true;
#else
    // On Unix OSes, look for Wayland or X
    return WMUtils::isRunningWayland() || WMUtils::isRunningX11();
#endif
}

bool WMUtils::isRunningDesktopEnvironment()
{
    // if (qEnvironmentVariableIsSet("HAS_DESKTOP_ENVIRONMENT")) {
    //     return qEnvironmentVariableIntValue("HAS_DESKTOP_ENVIRONMENT");
    // }

    // 检测操作系统
    bool isWindows = false;
    bool isMacOS = false;
    bool isEmbedded = false;

#if defined(_WIN32) || defined(_WIN64)
    isWindows = true;
#elif defined(__APPLE__) && defined(__MACH__)
    isMacOS = true;
#elif defined(__EMBEDDED__)
    isEmbedded = true;
#endif

    if (isWindows || isMacOS)
    {
        return true;
    }
    else if (isEmbedded)
    {
        return false;
    }
    else
    {
        return isRunningDesktopEnvironment();
    }
}

std::string WMUtils::getDrmCardOverride()
{
#ifdef HAVE_DRM
    QDir dir("/dev/dri");
    QStringList cardList = dir.entryList(QStringList("card*"), QDir::Files | QDir::System);
    if (cardList.length() == 0) {
        return QString();
    }

    bool needsOverride = false;
    for (const QString& card : cardList) {
        QFile cardFd(dir.filePath(card));
        if (!cardFd.open(QFile::ReadOnly)) {
            continue;
        }

        auto resources = drmModeGetResources(cardFd.handle());
        if (resources == nullptr) {
            // If we find a card that doesn't have a display before a card that
            // has one, we'll need to override Qt's EGLFS config because they
            // don't properly handle cards without displays.
            needsOverride = true;
        }
        else {
            // We found a card with a display
            drmModeFreeResources(resources);
            if (needsOverride) {
                // Override the default card with this one
                return dir.filePath(card);
            }
            else {
                return QString();
            }
        }
    }
#endif

    return std::string();
}

bool WMUtils::startsWith(const std::string& str, const std::string prefix) {
    return (str.rfind(prefix, 0) == 0);
}

bool WMUtils::endsWith(const std::string& str, const std::string& end) {
    int srclen = str.size();
    int endlen = end.size();
    if(srclen >= endlen)
    {
        std::string temp = str.substr(srclen - endlen, endlen);
        if(temp == end)
            return true;
    }

    return false;
}

bool WMUtils::containsSubstring(const std::string& str, const std::string& substr) {
    return str.find(substr) != std::string::npos;
}

std::string StringUtils::stringToHex(const std::string& input)
{
    std::string output;
    boost::algorithm::hex(input.begin(), input.end(), std::back_inserter(output));

    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return output;
}

std::string StringUtils::stringToHex(const std::string& input, char separator)
{
    std::string output;
    boost::algorithm::hex(input.begin(), input.end(), std::back_inserter(output));

    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (separator != '\0' && separator != '\n' && separator != '\t' && separator != '\r') {
        for (size_t i = 2; i < output.size(); i += 3) {
            output.insert(i, 1, separator);
        }
    }

    return output;
}

bool StringUtils::isValidIPv4(const std::string& ipstr)
{
    std::regex pattern(
        R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)"
        );

    return std::regex_match(ipstr, pattern);
}

bool StringUtils::isValidIPv6(const std::string& ipstr)
{
    std::regex pattern(
        R"(\b(?:[0-9a-fA-F]{1,4}::?){0,5}[0-9a-fA-F]{1,4}\b)"
        );

    return std::regex_match(ipstr, pattern);
}

std::string StringUtils::replacePlaceholder(const std::string& value, const std::string& from, const std::string& to)
{
    std::string result = value;
    size_t start_pos = 0;
    while ((start_pos = result.find(from, start_pos)) != std::string::npos) {
        result.replace(start_pos, from.length(), to);
        start_pos += to.length(); // 移动到替换后的位置，避免无限循环
    }
    return result;
}

bool StringUtils::caseInsensitiveCompare(const std::string& str1, const std::string& str2) {
    if (str1.size() != str2.size()) {
        return false;
    }
    for (size_t i = 0; i < str1.size(); ++i) {
        if (std::tolower(str1[i]) != std::tolower(str2[i])) {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
std::wstring StringUtils::stringToWString(const std::string& str)
{
    int wstrLength = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    if (wstrLength == 0) {
        return L"";
    }

    std::wstring wstr(wstrLength - 1, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], wstrLength) == 0) {
        return L"";
    }

    return wstr;
}
#endif

std::string Crypt::calculateSHA1(const std::string& input)
{
    unsigned char hash[SHA_DIGEST_LENGTH]; // SHA_DIGEST_LENGTH 为 20 字节
    SHA1(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);

    // 将哈希结果存储到 std::string 中
    return std::string(reinterpret_cast<char*>(hash), SHA_DIGEST_LENGTH);
}

std::string Crypt::calculateSHA256(const std::string& input)
{
    unsigned char hash[SHA256_DIGEST_LENGTH]; // SHA256_DIGEST_LENGTH 为 32 字节
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.length(), hash);

    // 将哈希结果存储到 std::string 中
    return std::string(reinterpret_cast<char*>(hash), SHA256_DIGEST_LENGTH);
}

std::string Crypt::calculateMD5(const std::string& input)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, input.c_str(), input.length());
    MD5_Final(digest, &context);

    std::string binaryDigest(reinterpret_cast<const char*>(digest), MD5_DIGEST_LENGTH);
    return binaryDigest;
}

std::string Crypt::pkcs7Pad(const std::string& input, int block_size)
{
    int len = input.length();
    int padding_len = block_size - (len % block_size);

    std::string padded(input.length() + padding_len, '\0');

    memcpy(&padded[0], input.c_str(), len);
    memset(&padded[len], padding_len, padding_len);

    return padded;
}

std::string Uuid::createUuid()
{
    boost::uuids::random_generator generator; // 创建随机 UUID 生成器对象
    boost::uuids::uuid uuid = generator();    // 生成一个随机 UUID
    std::string uuidStr = boost::uuids::to_string(uuid); // 将 UUID 转换为字符串
    boost::erase_all(uuidStr, "-"); // 去除字符串中的连字符 '-'
    return uuidStr;
}

std::string Uuid::createUuidWithHyphens()
{
    boost::uuids::random_generator generator; // 创建随机 UUID 生成器对象
    boost::uuids::uuid uuid = generator();    // 生成一个随机 UUID
    std::string uuidStr = boost::uuids::to_string(uuid); // 将 UUID 转换为字符串
    return uuidStr;
}

std::string DateTime::currentDatatime()
{
    // 获取当前时间
    boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
    // 使用 Boost.DateTime 格式化时间为字符串（包含毫秒）
    return boost::posix_time::to_iso_extended_string(now);
}

int64_t DateTime::currentSecsSinceEpoch()
{
    auto now = std::chrono::system_clock::now();
    auto duration_since_epoch = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration_since_epoch).count();
}

int Environment::environmentVariableIntValue(const std::string& name, bool* ok)
{
    int value = 0;
    char* envValue = std::getenv(name.c_str());
    if (envValue == nullptr)
    {
        *ok = false;
    }
    else
    {
        try
        {
            value = std::stoi(envValue);
            *ok = true;
        }
        catch (...)
        {
            *ok = false;
        }
    }

    return value;
}

std::string Environment::environmentVariableStringValue(const std::string& name, bool* ok)
{
    std::string value;
    char* envValue = std::getenv(name.c_str());
    if (envValue != nullptr)
    {
        value = envValue;
        *ok = true;
    }
    else
    {
        *ok = false;
    }

    return value;
}

std::string Base64::encode(const std::vector<unsigned char>& data)
{
    std::string encoded;
    encoded.resize(boost::beast::detail::base64::encoded_size(data.size()));
    boost::beast::detail::base64::encode(&encoded[0], data.data(), data.size());
    return encoded;
}
