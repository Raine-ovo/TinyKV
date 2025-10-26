#include "common/config/config.h"

#include "common/logger/logger.h"
#include <cstdlib>
#include <fstream>
#include <string>
#include <toml/serializer.hpp>
#include <toml/value.hpp>

Config::Config()
{
    // 读取配置文件, 因为运行在 bin 目录，所以在上层
    std::ifstream config_file("../default_config.toml");
    if (!config_file.is_open())
    {
        LOG_ERROR("failed to open config file!");
        exit(EXIT_FAILURE);
    }
    auto data = toml::parse(config_file);
    
    // 递归遍历所有键值对(注意根目录，即整张表视为一个 table)
    traverse_toml(data, "");
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

void Config::traverse_toml(const toml::value &value, const std::string &prefix)
{
    if (value.is_table())
    {
        // 如果是 table 就转化为类类型，如 parent.son
        const auto& table = toml::get<toml::table>(value);
        for (const auto &[key, value]: table)
        {
            std::string current_key = prefix.empty() ? key : prefix + '.' + key;
            traverse_toml(value, current_key);
        }
    }
    else if (value.is_array())
    {
        // 如果是数组, 转化为数组类型，即 array[0]
        const auto& array = toml::get<toml::array>(value);
        for (size_t i = 0; i < array.size(); ++ i)
        {
            const std::string& current_key = prefix + '[' + std::to_string(i) + ']';
            traverse_toml(array[i], current_key);
        }
    }
    else
    {
        // 基本类型, 因为用的是 umap<string, string> 转化为 str
        // 这样会有一个问题：如果本身就是字符串，会把原来的双引号也带上
        // std::string value_str = toml::format(value);
        // _configMap[prefix] = value_str;
        std::string value_str;
        if (value.is_string()) value_str = toml::get<std::string>(value);
        else value_str = toml::format(value);

        _configMap[prefix] = value_str;
    }
}