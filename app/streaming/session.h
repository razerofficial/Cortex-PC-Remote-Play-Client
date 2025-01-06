#pragma once

#include <Limelight.h>
#include <opus_multistream.h>
#include "settings/streamingpreferences.h"
#include "input/input.h"
#include "video/decoder.h"
#include "audio/renderers/renderer.h"
#include "video/overlaymanager.h"

#include "boost/interprocess/sync/interprocess_semaphore.hpp"

class RazerSemaphore
{
public:
    RazerSemaphore(int initialCount = 0)
        : m_semaphore(initialCount) {}

    void notify() {
        m_semaphore.post();
    }

    void wait() {
        m_semaphore.wait();
    }

    bool tryWait() {
        return m_semaphore.try_wait();
    }

private:
    boost::interprocess::interprocess_semaphore m_semaphore;
};

class Session
{

    friend class SdlInputHandler;
    friend class DeferredSessionCleanupTask;
    friend class AsyncConnectionStartThread;
    friend class ExecThread;

public:
    enum State
    {
        NoError = 0,
        SessionInitErr,
        ConnectionErr,
        SDLWinCreateErr,
        ResetErr,
        AbnormalExit,
        NormalExit
    };

    explicit Session(NvComputer* computer, NvApp& app, StreamingPreferences *preferences = nullptr);

    // NB: This may not get destroyed for a long time! Don't put any cleanup here.
    // Use Session::exec() or DeferredSessionCleanupTask instead.
    virtual ~Session() {};

    void exec();

    static
    void getDecoderInfo(SDL_Window* window,
                        bool& isHardwareAccelerated, bool& isFullScreenOnly,
                        bool& isHdrSupported, RazerSize& maxResolution);

    static Session* get()
    {
        return s_ActiveSession;
    }

    static bool isBusy()
    {
        std::lock_guard<std::mutex> locker(Session::s_busyMutex);
        return Session::s_busy;
    }

    static bool tryAcquireSessionControl()
    {
        std::lock_guard<std::mutex> locker(Session::s_busyMutex);
        if (!Session::s_busy)
        {
            Session::s_state = Session::NoError;
            Session::s_busy = true;
            return true;
        }

        return false;
    }

    static void releaseSessionControl()
    {
        std::lock_guard<std::mutex> locker(Session::s_busyMutex);
        Session::s_busy = false;
    }

    static void Quit()
    {
        if (Session::isBusy())
        {
            // Push a quit event to the main loop
            SDL_Event event;
            event.type = SDL_QUIT;
            event.quit.timestamp = SDL_GetTicks();
            SDL_PushEvent(&event);

            s_ActiveSessionSemaphore.wait();
            s_ActiveSessionSemaphore.notify();
        }
    }

    static State streamingState()
    {
        return s_state;
    }

    static std::string streamingErrorText()
    {
        return s_streamErrorText;
    }

    Overlay::OverlayManager& getOverlayManager()
    {
        return m_OverlayManager;
    }

    void flushWindowEvents();

private:
    void execInternal();

    bool initialize();

    bool startConnectionAsync();

    bool validateLaunch(SDL_Window* testWindow);

    void emitLaunchWarning(std::string text);

    bool populateDecoderProperties(SDL_Window* window);

    IAudioRenderer* createAudioRenderer(const POPUS_MULTISTREAM_CONFIGURATION opusConfig);

    bool initializeAudioRenderer();

    bool testAudio(int audioConfiguration);

    int getAudioRendererCapabilities(int audioConfiguration);

    void getWindowDimensions(int& x, int& y,
                             int& width, int& height);

    void toggleFullscreen();

    void notifyMouseEmulationMode(bool enabled);

    void updateOptimalWindowDisplayMode();

    static
    bool isHardwareDecodeAvailable(SDL_Window* window,
                                   StreamingPreferences::VideoDecoderSelection vds,
                                   int videoFormat, int width, int height, int frameRate);

    static
    bool chooseDecoder(StreamingPreferences::VideoDecoderSelection vds,
                       SDL_Window* window, int videoFormat, int width, int height,
                       int frameRate, bool enableVsync, bool enableFramePacing,
                       bool testOnly,
                       IVideoDecoder*& chosenDecoder);

    static
    void clStageStarting(int stage);

    static
    void clStageFailed(int stage, int errorCode);

    static
    void clConnectionTerminated(int errorCode);

    static
    void clLogMessage(const char* format, ...);

    static
    void clRumble(unsigned short controllerNumber, unsigned short lowFreqMotor, unsigned short highFreqMotor);

    static
    void clConnectionStatusUpdate(int connectionStatus);

    static
    void clSetHdrMode(bool enabled);

    static
    void clRumbleTriggers(uint16_t controllerNumber, uint16_t leftTrigger, uint16_t rightTrigger);

    static
    void clSetMotionEventState(uint16_t controllerNumber, uint8_t motionType, uint16_t reportRateHz);

    static
    void clSetControllerLED(uint16_t controllerNumber, uint8_t r, uint8_t g, uint8_t b);

    static
    int arInit(int audioConfiguration,
               const POPUS_MULTISTREAM_CONFIGURATION opusConfig,
               void* arContext, int arFlags);

    static
    void arCleanup();

    static
    void arDecodeAndPlaySample(char* sampleData, int sampleLength);

    static
    int drSetup(int videoFormat, int width, int height, int frameRate, void*, int);

    static
    void drCleanup();

    static
    int drSubmitDecodeUnit(PDECODE_UNIT du);

    StreamingPreferences* m_Preferences;
    bool m_IsFullScreen;
    STREAM_CONFIGURATION m_StreamConfig;
    DECODER_RENDERER_CALLBACKS m_VideoCallbacks;
    AUDIO_RENDERER_CALLBACKS m_AudioCallbacks;
    NvComputer* m_Computer;
    NvApp m_App;
    SDL_Window* m_Window;
    IVideoDecoder* m_VideoDecoder;
    SDL_SpinLock m_DecoderLock;
    bool m_AudioDisabled;
    bool m_AudioMuted;
    Uint32 m_FullScreenFlag;
    bool m_ThreadedExec;
    bool m_UnexpectedTermination;
    SdlInputHandler* m_InputHandler;
    int m_MouseEmulationRefCount;
    int m_FlushingWindowEventsRef;

    bool m_AsyncConnectionSuccess;
    int m_PortTestResults;

    int m_ActiveVideoFormat;
    int m_ActiveVideoWidth;
    int m_ActiveVideoHeight;
    int m_ActiveVideoFrameRate;

    OpusMSDecoder* m_OpusDecoder;
    IAudioRenderer* m_AudioRenderer;
    OPUS_MULTISTREAM_CONFIGURATION m_ActiveAudioConfig;
    OPUS_MULTISTREAM_CONFIGURATION m_OriginalAudioConfig;
    int m_AudioSampleCount;
    Uint32 m_DropAudioEndTime;

    Overlay::OverlayManager m_OverlayManager;

    static bool s_busy;
    static std::mutex s_busyMutex;
    static State s_state;
    static std::string s_stageText;
    static std::string s_streamErrorText;
    static CONNECTION_LISTENER_CALLBACKS k_ConnCallbacks;
    static Session* s_ActiveSession;
    static RazerSemaphore s_ActiveSessionSemaphore;
};
