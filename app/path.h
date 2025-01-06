#pragma once

#include <filesystem>

class Path
{
public:
    static std::string getLogDir();
    static std::string getBoxArtCacheDir();

    static std::string readDataFile(std::string fileName);
    static void deleteCacheFile(std::string fileName);

    static std::string getDataFilePath(std::string fileName);

    static void initialize();

private:
    static std::filesystem::path s_CacheDir;
    static std::filesystem::path s_LogDir;
    static std::filesystem::path s_BoxArtCacheDir;
};
