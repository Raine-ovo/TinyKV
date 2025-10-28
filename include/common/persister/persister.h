#ifndef PERSISTER_H
#define PERSISTER_H

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <shared_mutex>
#include <string>
#include <filesystem>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

// 使用 RAII 实现文件写入
class FileWriter
{
public:
    explicit FileWriter(const fs::path &path);
    ~FileWriter();
    // 这里实现对 int、float 等基本类型的写入，因为字节数不同，使用模板
    template<typename T> void write(const T &data)
    {
        _file.write(reinterpret_cast<const char*>(&data), sizeof(T));
    }
    void write_string(const std::string &str);
    void write_bytes(const std::vector<uint8_t> &data);
    bool is_good();

private:
    std::ofstream _file;
    fs::path _temp_path;
    fs::path _final_path;
};

class Persister
{
public:
    explicit Persister( const std::string &state_file = "../data/state", 
        const std::string &snapshot_file = "../data/snapshot");
    ~Persister();
    
    bool save_state(uint64_t current_term, uint32_t voted_for, const std::vector<uint8_t> &log_data);
    std::optional<std::tuple<uint64_t, uint32_t, std::vector<uint8_t>>> load_state();

    bool save_snapshot(const std::vector<uint8_t> &snapshot_data);
    std::optional<std::vector<uint8_t>> load_snapshot();

private:
    std::shared_mutex _mutex; // 临界资源访问

    fs::path _state_file;
    fs::path _snapshot_file;
};

#endif