#include "common/config/config.h"

Config::Config()
{

}

Config::~Config()
{
    
}

Config& Config::getInstance()
{
    static Config config;
    return config;
}

void Config::init(const std::string& file_path)
{

}

std::string Config::Load(const std::string& key)
{
    if (!_configMap.contains(key))
    {
        return "";
    }
    return _configMap[key];
}