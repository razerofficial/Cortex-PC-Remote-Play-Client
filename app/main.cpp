// Don't let SDL hook our main function, since Qt is already
// doing the same thing. This needs to be before any headers
// that might include SDL.h themselves.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#ifdef HAVE_FFMPEG
#include "streaming/video/ffmpeg.h"
#endif

#if defined(_WIN32) || defined(_WIN64)
#include "antihookingprotection.h"
#endif


#include "path.h"
#include "utils.h"
#include "mainwindow.h"
#include "backend/computermanager.h"
#include "backend/systemproperties.h"
#include "backend/identitymanager.h"
#include <glog/logging.h>

#if defined(_WIN32) || defined(_WIN64)
#define IS_UNSPECIFIED_HANDLE(x) ((x) == INVALID_HANDLE_VALUE || (x) == NULL)
#else
#endif

static std::mutex s_LoggerLock;
static bool s_SuppressVerboseOutput;

void logToLoggerStream(std::string& message, int level)
{
    std::lock_guard<std::mutex> locker(s_LoggerLock);

    switch (level)
    {
    case 0:
        LOG(INFO) << message;
        break;
    case 1:
        LOG(WARNING) << message;
        break;
    case 2:
        LOG(ERROR) << message;
        break;
    case 3:
        LOG(FATAL) << message;
        break;
    default:
        //LOG(INFO) << message;
        break;
    }
}

void sdlLogToDiskHandler(void*, int category, SDL_LogPriority priority, const char* message)
{
    std::string priorityTxt;
    int level = 0;

    switch (priority) {
    case SDL_LOG_PRIORITY_VERBOSE:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Verbose";
        level = 0;
        break;
    case SDL_LOG_PRIORITY_DEBUG:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Debug";
        level = 0;
        break;
    case SDL_LOG_PRIORITY_INFO:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Info";
        level = 0;
        break;
    case SDL_LOG_PRIORITY_WARN:
        if (s_SuppressVerboseOutput) {
            return;
        }
        priorityTxt = "Warn";
        level = 1;
        break;
    case SDL_LOG_PRIORITY_ERROR:
        priorityTxt = "Error";
        level = 2;
        break;
    case SDL_LOG_PRIORITY_CRITICAL:
        priorityTxt = "Critical";
        level = 3;
        break;
    default:
        priorityTxt = "Unknown";
        break;
    }

    std::stringstream ss;
    ss << " SDL " << priorityTxt << category << " : "<< message;
    std::string txt = ss.str();
    logToLoggerStream(txt, level);
}

#ifdef HAVE_FFMPEG

void ffmpegLogToDiskHandler(void* ptr, int level, const char* fmt, va_list vl)
{
    char lineBuffer[1024];
    static int printPrefix = 1;

    if ((level & 0xFF) > av_log_get_level()) {
        return;
    }
    else if ((level & 0xFF) > AV_LOG_WARNING && s_SuppressVerboseOutput) {
        return;
    }

    // We need to use the *previous* printPrefix value to determine whether to
    // print the prefix this time. av_log_format_line() will set the printPrefix
    // value to indicate whether the prefix should be printed *next time*.
    bool shouldPrefixThisMessage = printPrefix != 0;

    av_log_format_line(ptr, level, fmt, vl, lineBuffer, sizeof(lineBuffer), &printPrefix);

    if (shouldPrefixThisMessage) {
        std::stringstream ss;
        ss << " FFmpeg: " << lineBuffer;
        std::string txt = ss.str();
        logToLoggerStream(txt, 0);
    }
    else {
        // std::stringstream ss;
        // ss << " FFmpeg: " << lineBuffer;
        // std::string txt = ss.str();
        // logToLoggerStream(txt, 0);
    }
}

#endif

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

static UINT s_HitUnhandledException = 0;

LONG WINAPI UnhandledExceptionHandler(struct _EXCEPTION_POINTERS *ExceptionInfo)
{
    // Only write a dump for the first unhandled exception
    if (InterlockedCompareExchange(&s_HitUnhandledException, 1, 0) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char dmpFileName[MAX_PATH];
    sprintf_s(dmpFileName, sizeof(dmpFileName), "%sRemotePlayClient-%lld.dmp",
        Path::getLogDir().c_str(), DateTime::currentSecsSinceEpoch());

    HANDLE dumpHandle = CreateFileA(dmpFileName, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dumpHandle != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info;

        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = ExceptionInfo;
        info.ClientPointers = FALSE;

        DWORD typeFlags = MiniDumpWithIndirectlyReferencedMemory |
                MiniDumpIgnoreInaccessibleMemory |
                MiniDumpWithUnloadedModules |
                MiniDumpWithThreadInfo;

        if (MiniDumpWriteDump(GetCurrentProcess(),
                               GetCurrentProcessId(),
                               dumpHandle,
                               (MINIDUMP_TYPE)typeFlags,
                               &info,
                               nullptr,
                               nullptr)) {
            LOG(ERROR) << "Unhandled exception! Minidump written to:" << dmpFileName;
        }
        else {
            LOG(ERROR) << "Unhandled exception! Failed to write dump:" << GetLastError();
        }

        CloseHandle(dumpHandle);
    }
    else {
        LOG(ERROR) << "Unhandled exception! Failed to open dump file:" << dmpFileName << "with error" << GetLastError();
    }

    // Let the program crash and WER collect a dump
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

class SingleInstanceChecker {
public:
    SingleInstanceChecker()
        : mutexName_(StringUtils::stringToWString(programName)), hMutex_(nullptr), isRunning_(false) {
        hMutex_ = CreateMutex(NULL, TRUE, mutexName_.c_str());

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            isRunning_ = false;
        }
        else {
            isRunning_ = true;
        }
    }

    ~SingleInstanceChecker() {
        if (hMutex_) {
            CloseHandle(hMutex_);
        }
    }

    bool isRunning() const {
        return isRunning_;
    }

private:
    std::wstring mutexName_;
    HANDLE hMutex_;
    bool isRunning_;
};


MyWindow g_window(L"MyWindow", StringUtils::stringToWString(programName).c_str());

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SingleInstanceChecker checker;

    if (!checker.isRunning())
        return -1;

    SDL_SetMainReady();

    Path::initialize();

    google::InitGoogleLogging(programName.c_str());
    google::EnableLogCleaner(7);
    FLAGS_log_dir = Path::getLogDir().c_str();
    FLAGS_logtostderr = false;

    SDL_LogSetOutputFunction(sdlLogToDiskHandler, nullptr);

#ifdef HAVE_FFMPEG
    av_log_set_callback(ffmpegLogToDiskHandler);
#endif

#if defined(_WIN32) || defined(_WIN64)
    // Create a crash dump when we crash on Windows
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
#endif

#if defined(_WIN32) || defined(_WIN64)
    // Force AntiHooking.dll to be statically imported and loaded
    // by ntdll on Win32 platforms by calling a dummy function.
    AntiHookingDummyImport();
#endif

    // Allow the display to sleep by default. We will manually use SDL_DisableScreenSaver()
    // and SDL_EnableScreenSaver() when appropriate. This hint must be set before
    // initializing the SDL video subsystem to have any effect.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");

    // For SDL backends that support it, use double buffering instead of triple buffering
    // to save a frame of latency. This doesn't matter for MMAL or DRM renderers since they
    // are drawing directly to the screen without involving SDL, but it may matter for other
    // future KMSDRM platforms that use SDL for rendering.
    SDL_SetHint(SDL_HINT_VIDEO_DOUBLE_BUFFER, "1");

    // We use MMAL to render on Raspberry Pi, so we do not require DRM master.
    SDL_SetHint("SDL_KMSDRM_REQUIRE_DRM_MASTER", "0");

    // Use Direct3D 9Ex to avoid a deadlock caused by the D3D device being reset when
    // the user triggers a UAC prompt. This option controls the software/SDL renderer.
    // The DXVA2 renderer uses Direct3D 9Ex itself directly.
    SDL_SetHint("SDL_WINDOWS_USE_D3D9EX", "1");

    if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_TIMER) failed: %s",
                     SDL_GetError());
        return -1;
    }

#ifdef STEAM_LINK
    // Steam Link requires that we initialize video before creating our
    // QGuiApplication in order to configure the framebuffer correctly.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: %s",
                     SDL_GetError());
        return -1;
    }
#endif

    // Use atexit() to ensure SDL_Quit() is called. This avoids
    // racing with object destruction where SDL may be used.
    atexit(SDL_Quit);

    // Avoid the default behavior of changing the timer resolution to 1 ms.
    // We don't want this all the time that Moonlight is open. We will set
    // it manually when we start streaming.
    SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "0");

    // Disable minimize on focus loss by default. Users seem to want this off by default.
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");

    // SDL 2.0.12 changes the default behavior to use the button label rather than the button
    // position as most other software does. Set this back to 0 to stay consistent with prior
    // releases of Moonlight.
    SDL_SetHint("SDL_GAMECONTROLLER_USE_BUTTON_LABELS", "0");

    // Disable relative mouse scaling to renderer size or logical DPI. We want to send
    // the mouse motion exactly how it was given to us.
    SDL_SetHint("SDL_MOUSE_RELATIVE_SCALING", "0");

    // Set our app name for SDL to use with PulseAudio and PipeWire. This matches what we
    // provide as our app name to libsoundio too. On SDL 2.0.18+, SDL_APP_NAME is also used
    // for screensaver inhibitor reporting.
    SDL_SetHint("SDL_AUDIO_DEVICE_APP_NAME", "Moonlight");
    SDL_SetHint("SDL_APP_NAME", "Moonlight");

    // We handle capturing the mouse ourselves when it leaves the window, so we don't need
    // SDL doing it for us behind our backs.
    SDL_SetHint("SDL_MOUSE_AUTO_CAPTURE", "0");

    // SDL will try to lock the mouse cursor on Wayland if it's not visible in order to
    // support applications that assume they can warp the cursor (which isn't possible
    // on Wayland). We don't want this behavior because it interferes with seamless mouse
    // mode when toggling between windowed and fullscreen modes by unexpectedly locking
    // the mouse cursor.
    SDL_SetHint("SDL_VIDEO_WAYLAND_EMULATE_MOUSE_WARP", "0");

#ifdef _DEBUG
    // Allow thread naming using exceptions on debug builds. SDL doesn't use SEH
    // when throwing the exceptions, so we don't enable it for release builds out
    // of caution.
    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "0");
#endif

    SDL_version compileVersion;
    SDL_VERSION(&compileVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Compiled with SDL %d.%d.%d",
                compileVersion.major, compileVersion.minor, compileVersion.patch);

    SDL_version runtimeVersion;
    SDL_GetVersion(&runtimeVersion);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Running with SDL %d.%d.%d",
                runtimeVersion.major, runtimeVersion.minor, runtimeVersion.patch);

    SystemProperties::get();

    // Create the identity manager on the main thread
    IdentityManager::get();

    ComputerManager::getInstance()->startPolling();

    MSG msg = { 0 };
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ComputerManager::getInstance()->destroyInstance();
    LOG(INFO) << "RemotePlayClient graceful exit!";
    google::ShutdownGoogleLogging();

    return static_cast<int>(msg.wParam);
}
