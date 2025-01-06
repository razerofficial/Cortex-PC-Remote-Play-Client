#include "path.h"
#include "utils.h"
#include <fstream>
#include <cassert>

#include <glog/logging.h>
#include <boost/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

std::filesystem::path Path::s_CacheDir;
std::filesystem::path Path::s_LogDir;
std::filesystem::path Path::s_BoxArtCacheDir;

std::string Path::getLogDir()
{
    std::string logDir = s_LogDir.string();
    assert(!logDir.empty());
    return logDir;
}

std::string Path::getBoxArtCacheDir()
{
    std::string boxArtDir = s_BoxArtCacheDir.string();
    assert(!boxArtDir.empty());
    return boxArtDir;
}

std::string Path::readDataFile(std::string fileName)
{
    std::string absPath = Path::getDataFilePath(fileName);

    std::string content;
    std::ifstream file(absPath, std::ios::binary);
    if (!file.is_open())
        return content;

    content = std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    return content;
}

void Path::deleteCacheFile(std::string fileName)
{
    std::string path = s_CacheDir.string() + fileName;
    std::filesystem::remove(path);
}

std::string Path::getDataFilePath(std::string fileName)
{
    std::string candidatePath;

    // Check the cache location first (used by Path::writeDataFile())
    candidatePath = s_CacheDir.string() + fileName;
    if (std::filesystem::exists(candidatePath))
    {
        LOG(INFO) << "Found" << fileName << "at" << candidatePath;
        return candidatePath;
    }

    // Return current floder data
    candidatePath = boost::dll::program_location().parent_path().string() + "\\data\\" + fileName;
    if (std::filesystem::exists(candidatePath))
    {
        LOG(INFO) << "Found" << fileName << "at" << candidatePath;
        return candidatePath;
    }

    LOG(ERROR) << "NOT Found " << fileName;
    assert(false);
    return candidatePath;
}

void Path::initialize()
{
    bool ok;
    std::string localAppData = Environment::environmentVariableStringValue("LOCALAPPDATA", &ok);
    if (!ok)
    {
        s_LogDir.clear();
        s_CacheDir.clear();
        s_BoxArtCacheDir.clear();
    }
    else
    {
        s_LogDir = localAppData + "\\Razer\\Razer Cortex\\Log\\";
        s_CacheDir = localAppData + "\\Razer\\Razer Cortex\\RemotePlay\\Client\\cache";
        s_BoxArtCacheDir = localAppData + "\\Razer\\Razer Cortex\\RemotePlay\\Client\\boxart";
    }   
}
