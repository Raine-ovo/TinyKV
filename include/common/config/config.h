// 配置类，读取系统的相关配置

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <unordered_map>

#include "toml.hpp"

class Config
{
public:
    static Config& getInstance();
    void init(const std::string& file_path);
    std::string Load(const std::string& key);

private:
    std::unordered_map<std::string, std::string> _configMap;

    void traverse_toml(const toml::value &value, const std::string &prefix);

    // 单例模式
    Config();
    ~Config();
    Config(const Config&) = delete;
    Config(Config&&) = delete;
};

#endif