#pragma once

//#include "mappingfetcher.h"
#include <map>
#include <vector>
#include "boost/algorithm/string.hpp"

class SdlGamepadMapping
{
public:
    SdlGamepadMapping() {}

    SdlGamepadMapping(std::string string)
    {
        std::vector<std::string> mapping;
        boost::split(mapping, string, boost::is_any_of(","));
        if(!mapping.empty())
        {
            m_Guid = mapping[0];

            string.erase(0, m_Guid.length() + 1);
            m_Mapping = string;
        }
    }

    SdlGamepadMapping(std::string guid, std::string mapping)
        : m_Guid(guid),
          m_Mapping(mapping)
    {

    }

    bool operator==(const SdlGamepadMapping& other) const
    {
        return m_Guid == other.m_Guid && m_Mapping == other.m_Mapping;
    }

    std::string getGuid() const
    {
        return m_Guid;
    }

    std::string getMapping() const
    {
        return m_Mapping;
    }

    std::string getSdlMappingString() const
    {
        if (m_Guid.empty() || m_Mapping.empty()) {
            return "";
        }
        else {
            return m_Guid + "," + m_Mapping;
        }
    }

private:
    std::string m_Guid;
    std::string m_Mapping;
};

class MappingManager
{
public:
    MappingManager();

    void addMapping(std::string gamepadString);

    void addMapping(SdlGamepadMapping& gamepadMapping);

    void applyMappings();

    void save();

private:
    std::map<std::string, SdlGamepadMapping> m_Mappings;
};

