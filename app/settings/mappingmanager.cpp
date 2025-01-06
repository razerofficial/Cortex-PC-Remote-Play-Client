#include "mappingmanager.h"
#include "path.h"

#include <SDL.h>

#define SER_GAMEPADMAPPING "gcmapping"

#define SER_GUID "guid"
#define SER_MAPPING "mapping"

//MappingFetcher* MappingManager::s_MappingFetcher;

MappingManager::MappingManager()
{
    // QSettings settings;

    // // Load updated mappings from the Internet once per Moonlight launch
    // // if (s_MappingFetcher == nullptr) {
    // //     s_MappingFetcher = new MappingFetcher();
    // //     s_MappingFetcher->start();
    // // }

    // // First load existing saved mappings. This ensures the user's
    // // hints can always override the old data.
    // int mappingCount = settings.beginReadArray(SER_GAMEPADMAPPING);
    // for (int i = 0; i < mappingCount; i++) {
    //     settings.setArrayIndex(i);

    //     SdlGamepadMapping mapping(settings.value(SER_GUID).toString().toStdString(), settings.value(SER_MAPPING).toString().toStdString());
    //     addMapping(mapping);
    // }
    // settings.endArray();

    // Finally load mappings from SDL_HINT_GAMECONTROLLERCONFIG
    std::vector<std::string> sdlMappings;
    const char* pHint = SDL_GetHint(SDL_HINT_GAMECONTROLLERCONFIG);
    if (pHint)
    {
        boost::split(sdlMappings, std::string(pHint), boost::is_any_of("\n"));
        for (const std::string& sdlMapping : sdlMappings) {
            SdlGamepadMapping mapping(sdlMapping);
            addMapping(mapping);
        }
    }

    // Save the updated mappings to settings
    save();
}

void MappingManager::save()
{
    // QSettings settings;

    // settings.remove(SER_GAMEPADMAPPING);
    // settings.beginWriteArray(SER_GAMEPADMAPPING);
    // // QList<SdlGamepadMapping> mappings = m_Mappings.values();
    // // for (int i = 0; i < mappings.count(); i++) {
    // //     settings.setArrayIndex(i);

    // //     settings.setValue(SER_GUID, QString::fromStdString(mappings[i].getGuid()));
    // //     settings.setValue(SER_MAPPING, QString::fromStdString(mappings[i].getMapping()));
    // // }
    // std::vector<SdlGamepadMapping> mappings;
    // std::transform(m_Mappings.begin(), m_Mappings.end(), std::back_inserter(mappings),
    //                [](const std::pair<const std::string, SdlGamepadMapping>& pair) { return pair.second; });
    // for (int i = 0; i < mappings.size(); i++) {
    //     settings.setArrayIndex(i);

    //     settings.setValue(SER_GUID, QString::fromStdString(mappings.at(i).getGuid()));
    //     settings.setValue(SER_MAPPING, QString::fromStdString(mappings[i].getMapping()));
    // }
    // settings.endArray();
}

void MappingManager::applyMappings()
{
    std::string mappingData = Path::readDataFile("gamecontrollerdb.txt");
    if (!mappingData.empty()) {
        int newMappings = SDL_GameControllerAddMappingsFromRW(
                    SDL_RWFromConstMem(mappingData.data(), mappingData.size()), 1);

        if (newMappings > 0) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded %d new gamepad mappings",
                        newMappings);
        }
        else {
            if (newMappings < 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                             "Error loading gamepad mappings: %s",
                             SDL_GetError());
            }
            else if (newMappings == 0) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "0 new mappings found in gamecontrollerdb.txt. Is it corrupt?");
            }

            // Try deleting the cached mapping list just in case it's corrupt
            Path::deleteCacheFile("gamecontrollerdb.txt");
        }
    }
    else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Unable to load gamepad mapping file");
    }

    //QList<SdlGamepadMapping> mappings = m_Mappings.values();
    std::list<SdlGamepadMapping> mappings;
    std::transform(m_Mappings.begin(), m_Mappings.end(), std::back_inserter(mappings),
                   [](const std::pair<const std::string, SdlGamepadMapping>& pair) { return pair.second; });
    for (const SdlGamepadMapping& mapping : mappings) {
        std::string sdlMappingString = mapping.getSdlMappingString();
        int ret = SDL_GameControllerAddMapping(sdlMappingString.c_str());
        if (ret < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Unable to add mapping: %s",
                        sdlMappingString.c_str());
        }
        else if (ret == 1) {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "Loaded saved user mapping: %s",
                        sdlMappingString.c_str());
        }
    }
}

void MappingManager::addMapping(std::string mappingString)
{
    SdlGamepadMapping mapping(mappingString);
    addMapping(mapping);
}

void MappingManager::addMapping(SdlGamepadMapping& mapping)
{
    m_Mappings[mapping.getGuid()] = mapping;
}
