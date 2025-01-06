#pragma once

#include <string>
#include <filesystem>

#include <json.hpp>
#include <glog/logging.h>
#include <boost/property_tree/ptree.hpp>

class NvComputer;
class Configure
{
public:
    static Configure* getInstance();

    template<typename T>
    T getGeneral(const std::string& key) 
    {
        try 
        {
            return m_general.at(key).get<T>();
        }
        catch (const nlohmann::json::out_of_range& e)
        {
            LOG(ERROR) << "getGeneral key not found: " << key << e.what();
        }
        catch (const nlohmann::json::type_error& e)
        {
            LOG(ERROR) << "getGeneral type error for key: " << key << ". expected type: " << typeid(T).name() << e.what();
        }
        catch (const std::exception& e)
        {
            LOG(ERROR) << "getGeneral exception: " << e.what();
        }
    }

    template<typename T>
    void setGeneral(const std::string& key, const T& value)
    {
        try
        {
            m_general[key] = value;
        }
        catch (const std::exception& e) 
        {
            LOG(ERROR) << "setGeneral exception: " << e.what();
        }
    }
    void saveGeneral();

    void saveHosts();
    int getHostsSize();
    void setHostsSize(int size);
    boost::property_tree::ptree getHosts();
    void setHosts(const boost::property_tree::ptree& hosts);

private:
    Configure();
    Configure(const Configure&) = delete;
    Configure& operator=(const Configure&) = delete;

private:
    std::filesystem::path m_generalPath;
    nlohmann::json m_general;

    std::filesystem::path m_hostsPath;
    boost::property_tree::ptree m_hosts;
};





