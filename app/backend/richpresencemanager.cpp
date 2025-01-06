#include "richpresencemanager.h"

#include "glog/logging.h"

RichPresenceManager::RichPresenceManager(StreamingPreferences& prefs, std::string gameName)
    : m_DiscordActive(false)
{
#ifdef HAVE_DISCORD
    if (prefs.richPresence) {
        DiscordEventHandlers handlers = {};
        handlers.ready = discordReady;
        handlers.disconnected = discordDisconnected;
        handlers.errored = discordErrored;
        Discord_Initialize("594668102021677159", &handlers, 0, nullptr);
        m_DiscordActive = true;
    }

    if (m_DiscordActive) {
        std::string stateStr = "Streaming " + gameName;

        DiscordRichPresence discordPresence = {};
        discordPresence.state = stateStr.data();
        discordPresence.startTimestamp = time(nullptr);
        discordPresence.largeImageKey = "icon";
        Discord_UpdatePresence(&discordPresence);
    }
#else
    static_cast<void>(prefs);
    static_cast<void>(gameName);
#endif
}

RichPresenceManager::~RichPresenceManager()
{
#ifdef HAVE_DISCORD
    if (m_DiscordActive) {
        Discord_ClearPresence();
        Discord_Shutdown();
    }
#endif
}

void RichPresenceManager::runCallbacks()
{
#ifdef HAVE_DISCORD
    if (m_DiscordActive) {
        Discord_RunCallbacks();
    }
#endif
}

#ifdef HAVE_DISCORD
void RichPresenceManager::discordReady(const DiscordUser* request)
{
    LOG(INFO) << "Discord integration ready for user:" << request->username;
}

void RichPresenceManager::discordDisconnected(int errorCode, const char *message)
{
    LOG(INFO) << "Discord integration disconnected:" << errorCode << message;
}

void RichPresenceManager::discordErrored(int errorCode, const char *message)
{
    LOG(WARNING) << "Discord integration error:" << errorCode << message;
}
#endif
