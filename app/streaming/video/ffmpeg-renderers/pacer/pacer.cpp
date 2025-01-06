#include "pacer.h"
#include "streaming/streamutils.h"

//#ifdef Q_OS_WIN32
#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <VersionHelpers.h>
#include "dxvsyncsource.h"
#endif

#ifdef HAS_WAYLAND
#include "waylandvsyncsource.h"
#endif

#include <SDL_syswm.h>

// Limit the number of queued frames to prevent excessive memory consumption
// if the V-Sync source or renderer is blocked for a while. It's important
// that the sum of all queued frames between both pacing and rendering queues
// must not exceed the number buffer pool size to avoid running the decoder
// out of available decoding surfaces.
#define MAX_QUEUED_FRAMES 4

// We may be woken up slightly late so don't go all the way
// up to the next V-sync since we may accidentally step into
// the next V-sync period. It also takes some amount of time
// to do the render itself, so we can't render right before
// V-sync happens.
#define TIMER_SLACK_MS 3

Pacer::Pacer(IFFmpegRenderer* renderer, PVIDEO_STATS videoStats) :
    m_RenderThread(nullptr),
    m_VsyncThread(nullptr),
    m_Stopping(false),
    m_VsyncSource(nullptr),
    m_VsyncRenderer(renderer),
    m_MaxVideoFps(0),
    m_DisplayFps(0),
    m_VideoStats(videoStats)
{

}

Pacer::~Pacer()
{
    m_Stopping = true;

    // Stop the V-sync thread
    if (m_VsyncThread != nullptr) {
        m_PacingQueueNotEmpty.notify_all();
        m_VsyncSignalled.notify_all();
        SDL_WaitThread(m_VsyncThread, nullptr);
    }

    // Stop V-sync callbacks
    delete m_VsyncSource;
    m_VsyncSource = nullptr;

    // Stop the render thread
    if (m_RenderThread != nullptr) {
        m_RenderQueueNotEmpty.notify_all();
        SDL_WaitThread(m_RenderThread, nullptr);
    }
    else {
        // Notify the renderer that it is being destroyed soon
        // NB: This must happen on the same thread that calls renderFrame().
        m_VsyncRenderer->cleanupRenderContext();
    }

    // Delete any remaining unconsumed frames
    while (!m_RenderQueue.empty()) {
        AVFrame* frame = m_RenderQueue.front();
        m_RenderQueue.pop();
        av_frame_free(&frame);
    }
    while (!m_PacingQueue.empty()) {
        AVFrame* frame = m_PacingQueue.front();
        m_PacingQueue.pop();
        av_frame_free(&frame);
    }
}

void Pacer::renderOnMainThread()
{
    // Ignore this call for renderers that work on a dedicated render thread
    if (m_RenderThread != nullptr) {
        return;
    }

    m_FrameQueueLock.lock();

    if (!m_RenderQueue.empty()) {
        AVFrame* frame = m_RenderQueue.front();
        m_RenderQueue.pop();
        m_FrameQueueLock.unlock();

        renderFrame(frame);
    }
    else {
        m_FrameQueueLock.unlock();
    }
}

int Pacer::vsyncThread(void *context)
{
    Pacer* me = reinterpret_cast<Pacer*>(context);

#if SDL_VERSION_ATLEAST(2, 0, 9)
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
#else
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif

    bool async = me->m_VsyncSource->isAsync();
    while (!me->m_Stopping) {
        if (async) {
            // Wait for the VSync source to invoke signalVsync() or 100ms to elapse
            std::unique_lock<std::mutex> locker(me->m_FrameQueueLock);
            me->m_VsyncSignalled.wait_for(locker, std::chrono::milliseconds(100));
        }
        else {
            // Let the VSync source wait in the context of our thread
            me->m_VsyncSource->waitForVsync();
        }

        if (me->m_Stopping) {
            break;
        }

        me->handleVsync(1000 / me->m_DisplayFps);
    }

    return 0;
}

int Pacer::renderThread(void* context)
{
    Pacer* me = reinterpret_cast<Pacer*>(context);

    if (SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH) < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to set render thread to high priority: %s",
                    SDL_GetError());
    }

    while (!me->m_Stopping) {
        // Wait for the renderer to be ready for the next frame
        me->m_VsyncRenderer->waitToRender();

        // Acquire the frame queue lock to protect the queue and
        // the not empty condition
        std::unique_lock<std::mutex> locker(me->m_FrameQueueLock);

        // Wait for a frame to be ready to render
        while (!me->m_Stopping && me->m_RenderQueue.empty()) {
            me->m_RenderQueueNotEmpty.wait(locker);
        }

        if (me->m_Stopping) {
            // Exit this thread
            locker.unlock();
            break;
        }

        AVFrame* frame = me->m_RenderQueue.front();
        me->m_RenderQueue.pop();
        locker.unlock();

        me->renderFrame(frame);
    }

    // Notify the renderer that it is being destroyed soon
    // NB: This must happen on the same thread that calls renderFrame().
    me->m_VsyncRenderer->cleanupRenderContext();

    return 0;
}

void Pacer::enqueueFrameForRenderingAndUnlock(AVFrame *frame, std::shared_ptr<std::unique_lock<std::mutex>> spLocker)
{
    dropFrameForEnqueue(m_RenderQueue);
    m_RenderQueue.push(frame);

    spLocker->unlock();

    if (m_RenderThread != nullptr) {
        m_RenderQueueNotEmpty.notify_one();
    }
    else {
        SDL_Event event;

        // For main thread rendering, we'll push an event to trigger a callback
        event.type = SDL_USEREVENT;
        event.user.code = SDL_CODE_FRAME_READY;
        SDL_PushEvent(&event);
    }
}

// Called in an arbitrary thread by the IVsyncSource on V-sync
// or an event synchronized with V-sync
void Pacer::handleVsync(int timeUntilNextVsyncMillis)
{
    // Make sure initialize() has been called
    SDL_assert(m_MaxVideoFps != 0);

    std::shared_ptr<std::unique_lock<std::mutex>> spLocker(new std::unique_lock<std::mutex>(m_FrameQueueLock));

    // If the queue length history entries are large, be strict
    // about dropping excess frames.
    int frameDropTarget = 1;

    // If we may get more frames per second than we can display, use
    // frame history to drop frames only if consistently above the
    // one queued frame mark.
    if (m_MaxVideoFps >= m_DisplayFps) {
        // for (int queueHistoryEntry : m_PacingQueueHistory) {
        //     if (queueHistoryEntry <= 1) {
        //         // Be lenient as long as the queue length
        //         // resolves before the end of frame history
        //         frameDropTarget = 3;
        //         break;
        //     }
        // }
        std::queue<int> pacingQueueHistoryCopy(m_PacingQueueHistory);
        while (!pacingQueueHistoryCopy.empty()) {
            int queueHistoryEntry = pacingQueueHistoryCopy.front();
            pacingQueueHistoryCopy.pop();
            if (queueHistoryEntry <= 1) {
                // Be lenient as long as the queue length
                // resolves before the end of frame history
                frameDropTarget = 3;
                break;
            }
        }

        // Keep a rolling 500 ms window of pacing queue history
        if (m_PacingQueueHistory.size() == m_DisplayFps / 2) {
            m_PacingQueueHistory.pop();
        }

        m_PacingQueueHistory.push(m_PacingQueue.size());
    }

    // Catch up if we're several frames ahead
    while (m_PacingQueue.size() > frameDropTarget) {
        AVFrame* frame = m_PacingQueue.front();
        m_PacingQueue.pop();

        // Drop the lock while we call av_frame_free()
        spLocker->unlock();
        m_VideoStats->pacerDroppedFrames++;
        av_frame_free(&frame);
        spLocker->lock();
    }

    while (m_PacingQueue.empty()) {
        // Wait for a frame to arrive or our V-sync timeout to expire
        if (m_PacingQueueNotEmpty.wait_for(*spLocker.get(), std::chrono::milliseconds(SDL_max(timeUntilNextVsyncMillis, TIMER_SLACK_MS) - TIMER_SLACK_MS)) == std::cv_status::timeout) {
            // Wait timed out - unlock and bail
            spLocker->unlock();
            return;
        }

        if (m_Stopping) {
            spLocker->unlock();
            return;
        }
    }

    // Place the first frame on the render queue
    AVFrame *frame = m_PacingQueue.front();
    m_PacingQueue.pop();
    enqueueFrameForRenderingAndUnlock(frame, spLocker);
}

bool Pacer::initialize(SDL_Window* window, int maxVideoFps, bool enablePacing)
{
    m_MaxVideoFps = maxVideoFps;
    m_DisplayFps = StreamUtils::getDisplayRefreshRate(window);
    m_RendererAttributes = m_VsyncRenderer->getRendererAttributes();

    if (enablePacing) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Frame pacing: target %d Hz with %d FPS stream",
                    m_DisplayFps, m_MaxVideoFps);

        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL_GetWindowWMInfo() failed: %s",
                         SDL_GetError());
            return false;
        }

        switch (info.subsystem) {
    //#ifdef Q_OS_WIN32
    #if defined(_WIN32) || defined(_WIN64)
        case SDL_SYSWM_WINDOWS:
            // Don't use D3DKMTWaitForVerticalBlankEvent() on Windows 7, because
            // it blocks during other concurrent DX operations (like actually rendering).
            if (IsWindows8OrGreater()) {
                m_VsyncSource = new DxVsyncSource(this);
            }
            break;
    #endif

    #if defined(SDL_VIDEO_DRIVER_WAYLAND) && defined(HAS_WAYLAND)
        case SDL_SYSWM_WAYLAND:
            m_VsyncSource = new WaylandVsyncSource(this);
            break;
    #endif

        default:
            // Platforms without a VsyncSource will just render frames
            // immediately like they used to.
            break;
        }

        SDL_assert(m_VsyncSource != nullptr || !(m_RendererAttributes & RENDERER_ATTRIBUTE_FORCE_PACING));

        if (m_VsyncSource != nullptr && !m_VsyncSource->initialize(window, m_DisplayFps)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Vsync source failed to initialize. Frame pacing will not be available!");
            delete m_VsyncSource;
            m_VsyncSource = nullptr;
        }
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Frame pacing disabled: target %d Hz with %d FPS stream",
                    m_DisplayFps, m_MaxVideoFps);
    }

    if (m_VsyncSource != nullptr) {
        m_VsyncThread = SDL_CreateThread(Pacer::vsyncThread, "PacerVsync", this);
    }

    if (m_VsyncRenderer->isRenderThreadSupported()) {
        m_RenderThread = SDL_CreateThread(Pacer::renderThread, "PacerRender", this);
    }

    return true;
}

void Pacer::signalVsync()
{
    m_VsyncSignalled.notify_one();
}

void Pacer::renderFrame(AVFrame* frame)
{
    // Count time spent in Pacer's queues
    Uint32 beforeRender = SDL_GetTicks();
    m_VideoStats->totalPacerTime += beforeRender - frame->pkt_dts;

    // Render it
    m_VsyncRenderer->renderFrame(frame);
    Uint32 afterRender = SDL_GetTicks();

    m_VideoStats->totalRenderTime += afterRender - beforeRender;
    m_VideoStats->renderedFrames++;
    av_frame_free(&frame);

    // Drop frames if we have too many queued up for a while
    m_FrameQueueLock.lock();

    int frameDropTarget;

    if (m_RendererAttributes & RENDERER_ATTRIBUTE_NO_BUFFERING) {
        // Renderers that don't buffer any frames but don't support waitToRender() need us to buffer
        // an extra frame to ensure they don't starve while waiting to present.
        frameDropTarget = 1;
    }
    else {
        frameDropTarget = 0;
        // for (int queueHistoryEntry : m_RenderQueueHistory) {
        //     if (queueHistoryEntry == 0) {
        //         // Be lenient as long as the queue length
        //         // resolves before the end of frame history
        //         frameDropTarget = 2;
        //         break;
        //     }
        // }
        std::queue<int> renderQueueHistoryCopy(m_RenderQueueHistory);
        while (!renderQueueHistoryCopy.empty()) {
            int queueHistoryEntry = renderQueueHistoryCopy.front();
            renderQueueHistoryCopy.pop();
            if (queueHistoryEntry == 0) {
                // Be lenient as long as the queue length
                // resolves before the end of frame history
                frameDropTarget = 2;
                break;
            }
        }

        // Keep a rolling 500 ms window of render queue history
        if (m_RenderQueueHistory.size() == m_MaxVideoFps / 2) {
            m_RenderQueueHistory.pop();
        }

        m_RenderQueueHistory.push(m_RenderQueue.size());
    }

    // Catch up if we're several frames ahead
    while (m_RenderQueue.size() > frameDropTarget) {
        AVFrame* frame = m_RenderQueue.front();
        m_RenderQueue.pop();

        // Drop the lock while we call av_frame_free()
        m_FrameQueueLock.unlock();
        m_VideoStats->pacerDroppedFrames++;
        av_frame_free(&frame);
        m_FrameQueueLock.lock();
    }

    m_FrameQueueLock.unlock();
}

void Pacer::dropFrameForEnqueue(std::queue<AVFrame*>& queue)
{
    SDL_assert(queue.size() <= MAX_QUEUED_FRAMES);
    if (queue.size() == MAX_QUEUED_FRAMES) {
        AVFrame* frame = queue.front();
        queue.pop();
        av_frame_free(&frame);
    }
}

void Pacer::submitFrame(AVFrame* frame)
{
    // Make sure initialize() has been called
    SDL_assert(m_MaxVideoFps != 0);

    // Queue the frame and possibly wake up the render thread
    std::shared_ptr<std::unique_lock<std::mutex>> spLocker(new std::unique_lock<std::mutex>(m_FrameQueueLock));
    if (m_VsyncSource != nullptr) {
        dropFrameForEnqueue(m_PacingQueue);
        m_PacingQueue.push(frame);
        spLocker->unlock();
        m_PacingQueueNotEmpty.notify_one();
    }
    else {
        enqueueFrameForRenderingAndUnlock(frame, spLocker);
    }
}
