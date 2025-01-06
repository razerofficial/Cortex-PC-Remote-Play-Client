#pragma once

#include <string>
#include <vector>

#define THROW_BAD_ALLOC_IF_NULL(x) \
    if ((x) == nullptr) throw std::bad_alloc()

namespace WMUtils {
    bool isRunningX11();
    bool isRunningWayland();
    bool isRunningWindowManager();
    bool isRunningDesktopEnvironment();
    std::string getDrmCardOverride();

    bool startsWith(const std::string& str, const std::string prefix);
    bool endsWith(const std::string& str, const std::string& end);
    bool containsSubstring(const std::string& str, const std::string& substr);
}

namespace StringUtils {
    std::string stringToHex(const std::string& input);
    std::string stringToHex(const std::string& input, char separator);
    bool isValidIPv4(const std::string& ipstr);
    bool isValidIPv6(const std::string& ipstr);
    std::string replacePlaceholder(const std::string& value, const std::string& from, const std::string& to);
    bool caseInsensitiveCompare(const std::string& str1, const std::string& str2);
#ifdef _WIN32
    std::wstring stringToWString(const std::string& str);
#endif
}

namespace Crypt {
    std::string calculateSHA1(const std::string& input);
    std::string calculateSHA256(const std::string& input);
    std::string calculateMD5(const std::string& input);
    std::string pkcs7Pad(const std::string& input, int block_size);
}

namespace Uuid {
    std::string createUuid();
    std::string createUuidWithHyphens();
}

namespace DateTime {
    std::string currentDatatime();
    int64_t currentSecsSinceEpoch();
}

namespace Environment {
    int environmentVariableIntValue(const std::string& name, bool* ok);
    std::string environmentVariableStringValue(const std::string& name, bool* ok);
}

namespace Base64 {
    std::string encode(const std::vector<unsigned char>& data);
}

