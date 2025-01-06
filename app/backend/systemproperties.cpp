#include "systemproperties.h"
#include "utils.h"
#include "streaming/session.h"
#include "streaming/streamutils.h"

#include <boost/algorithm/string.hpp>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

std::string currentCpuArchitecture()
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    std::string arch;

    switch (sysInfo.wProcessorArchitecture)
    {
    case PROCESSOR_ARCHITECTURE_AMD64:
        arch = "x86_64";
        break;
    case PROCESSOR_ARCHITECTURE_INTEL:
        arch = "i386";
        break;
    case PROCESSOR_ARCHITECTURE_ARM64:
        arch = "arm64";
        break;
    default:
        arch = "Unknown";
        break;
    }

    return arch;
}

std::string buildCpuArchitecture() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386) || defined(_M_IX86)
    return "i386";
#elif defined(__aarch64__)
    return "arm64";
#else
    return "unknown";
#endif
}

SystemProperties* SystemProperties::get()
{
    static SystemProperties s_instance;
    return &s_instance;
}

SystemProperties::SystemProperties()
{
    versionString = std::string(VERSION_STR);
    hasDesktopEnvironment = WMUtils::isRunningDesktopEnvironment();
    isRunningWayland = WMUtils::isRunningWayland();
    isRunningXWayland = isRunningWayland && false;
    usesMaterial3Theme = false;
    std::string nativeArch = currentCpuArchitecture();

    wchar_t hostname[256];
    DWORD size = sizeof(hostname) / sizeof(hostname[0]);
    if (GetComputerNameW(hostname, &size)) {
        // Convert UTF-16 to UTF-8
        int utf8Size = WideCharToMultiByte(CP_UTF8, 0, hostname, -1, nullptr, 0, nullptr, nullptr);
        std::string utf8Hostname(utf8Size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, hostname, -1, &utf8Hostname[0], utf8Size, nullptr, nullptr);
        deviceName = utf8Hostname;
    }

#if defined(_WIN32) || defined(_WIN64)
    {
        USHORT processArch, machineArch;

        // Use IsWow64Process2 on TH2 and later, because it supports ARM64
        auto fnIsWow64Process2 = (decltype(IsWow64Process2)*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "IsWow64Process2");
        if (fnIsWow64Process2 != nullptr && fnIsWow64Process2(GetCurrentProcess(), &processArch, &machineArch)) {
            switch (machineArch) {
            case IMAGE_FILE_MACHINE_I386:
                nativeArch = "i386";
                break;
            case IMAGE_FILE_MACHINE_AMD64:
                nativeArch = "x86_64";
                break;
            case IMAGE_FILE_MACHINE_ARM64:
                nativeArch = "arm64";
                break;
            }
        }

        isWow64 = nativeArch != buildCpuArchitecture();
    }
#else
    isWow64 = false;
#endif

    if (nativeArch == "i386") {
        friendlyNativeArchName = "x86";
    }
    else if (nativeArch == "x86_64") {
        friendlyNativeArchName = "x64";
    }
    else {
        std::string tmp = nativeArch;
        friendlyNativeArchName = boost::to_upper_copy(tmp);
    }

    // Assume we can probably launch a browser if we're in a GUI environment
    hasBrowser = hasDesktopEnvironment;

#ifdef HAVE_DISCORD
    hasDiscordIntegration = true;
#else
    hasDiscordIntegration = false;
#endif

    unmappedGamepads = SdlInputHandler::getUnmappedGamepads();

    // Populate data that requires talking to SDL. We do it all in one shot
    // and cache the results to speed up future queries on this data.
    querySdlVideoInfo();

    assert(!monitorRefreshRates.empty());
    assert(!monitorNativeResolutions.empty());
    assert(!monitorSafeAreaResolutions.empty());
}

SDL_Rect SystemProperties::getNativeResolution(int displayIndex)
{
    // Returns default constructed QRect if out of bounds
    if(displayIndex < monitorNativeResolutions.size())
        return monitorNativeResolutions.at(displayIndex);

    return SDL_Rect();
}

SDL_Rect SystemProperties::getSafeAreaResolution(int displayIndex)
{
    // Returns default constructed QRect if out of bounds
    if(displayIndex < monitorSafeAreaResolutions.size())
        return monitorSafeAreaResolutions.at(displayIndex);

    return SDL_Rect();
}

int SystemProperties::getRefreshRate(int displayIndex)
{
    // Returns 0 if out of bounds
    if(displayIndex < monitorRefreshRates.size())
        return monitorRefreshRates.at(displayIndex);

    return 0;
}

class QuerySdlVideoThread {
public:
    QuerySdlVideoThread(SystemProperties* systemProperties) :
        m_SystemProperties(systemProperties) {}

    ~QuerySdlVideoThread() {
        if (m_Thread.joinable()) {
            m_Thread.join();
        }
    }

    void start() {
        m_Thread = std::thread(std::bind(&SystemProperties::querySdlVideoInfoInternal, m_SystemProperties));
    }

    void wait() {
        if (m_Thread.joinable()) {
            m_Thread.join();
        }
    }

private:
    SystemProperties* m_SystemProperties;
    std::thread m_Thread;
};

void SystemProperties::querySdlVideoInfo()
{
    if (WMUtils::isRunningX11() || WMUtils::isRunningWayland()) {
        // Use a separate thread to temporarily initialize SDL
        // video to avoid stomping on Qt's X11 and OGL state.
        QuerySdlVideoThread thread(this);
        thread.start();
        thread.wait();
    }
    else {
        querySdlVideoInfoInternal();
    }
}

void SystemProperties::querySdlVideoInfoInternal()
{
    hasHardwareAcceleration = false;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return;
    }

    // Update display related attributes (max FPS, native resolution, etc).
    // We call the internal variant because we're already in a safe thread context.
    refreshDisplaysInternal();

    SDL_Window* testWindow = SDL_CreateWindow("", 0, 0, 1280, 720,
                                              SDL_WINDOW_HIDDEN | StreamUtils::getPlatformWindowFlags());
    if (!testWindow) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Failed to create test window with platform flags: %s",
                    SDL_GetError());

        testWindow = SDL_CreateWindow("", 0, 0, 1280, 720, SDL_WINDOW_HIDDEN);
        if (!testWindow) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to create window for hardware decode test: %s",
                         SDL_GetError());
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }
    }

    Session::getDecoderInfo(testWindow, hasHardwareAcceleration, rendererAlwaysFullScreen, supportsHdr, maximumResolution);

    SDL_DestroyWindow(testWindow);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

class RefreshDisplaysThread {
public:
    RefreshDisplaysThread(SystemProperties* systemProperties) :
        m_SystemProperties(systemProperties) {}

    ~RefreshDisplaysThread() {
        if (m_Thread.joinable()) {
            m_Thread.join();
        }
    }

    void start() {
        m_Thread = std::thread(std::bind(&SystemProperties::refreshDisplaysInternal, m_SystemProperties));
    }

    void wait() {
        if (m_Thread.joinable()) {
            m_Thread.join();
        }
    }

private:
    SystemProperties* m_SystemProperties;
    std::thread m_Thread;
};

void SystemProperties::refreshDisplays()
{
    if (WMUtils::isRunningX11() || WMUtils::isRunningWayland()) {
        // Use a separate thread to temporarily initialize SDL
        // video to avoid stomping on Qt's X11 and OGL state.
        RefreshDisplaysThread thread(this);
        thread.start();
        thread.wait();
    }
    else {
        refreshDisplaysInternal();
    }
}

void SystemProperties::refreshDisplaysInternal()
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return;
    }

    monitorNativeResolutions.clear();
    monitorSafeAreaResolutions.clear();
    monitorRefreshRates.clear();

    SDL_DisplayMode bestMode;
    for (int displayIndex = 0; displayIndex < SDL_GetNumVideoDisplays(); displayIndex++) {
        SDL_DisplayMode desktopMode;
        SDL_Rect safeArea;

        if (StreamUtils::getNativeDesktopMode(displayIndex, &desktopMode, &safeArea)) {
            if (desktopMode.w <= 8192 && desktopMode.h <= 8192) {
                try {
                    monitorNativeResolutions.insert(monitorNativeResolutions.begin() + displayIndex, SDL_Rect(0, 0, desktopMode.w, desktopMode.h));
                    monitorSafeAreaResolutions.insert(monitorSafeAreaResolutions.begin() + displayIndex, SDL_Rect(0, 0, safeArea.w, safeArea.h));
                } catch (const std::bad_alloc& e) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "Memory allocation failed: %s",e.what());
                    continue;
                } catch (const std::exception& e) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "An exception occurred: %s", e.what());
                    continue;
                }
            }
            else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Skipping resolution over 8K: %dx%d",
                            desktopMode.w, desktopMode.h);
            }

            // Start at desktop mode and work our way up
            bestMode = desktopMode;
            for (int i = 0; i < SDL_GetNumDisplayModes(displayIndex); i++) {
                SDL_DisplayMode mode;
                if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0) {
                    if (mode.w == desktopMode.w && mode.h == desktopMode.h) {
                        if (mode.refresh_rate > bestMode.refresh_rate) {
                            bestMode = mode;
                        }
                    }
                }
            }

            // Try to normalize values around our our standard refresh rates.
            // Some displays/OSes report values that are slightly off.
            int normalizedRefreshRate = bestMode.refresh_rate;
            if (bestMode.refresh_rate >= 58 && bestMode.refresh_rate <= 62) {
                normalizedRefreshRate = 60;
            }
            else if (bestMode.refresh_rate >= 28 && bestMode.refresh_rate <= 32) {
                normalizedRefreshRate = 30;
            }

            monitorRefreshRates.push_back(normalizedRefreshRate);

            // Save the best mode for the primary display (index 0)
            if (displayIndex == 0) {
                primaryMonitorDisplayMode = NvDisplayMode(bestMode.w, bestMode.h, normalizedRefreshRate);
            }
        }
    }

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
