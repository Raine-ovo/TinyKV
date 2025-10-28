#include "common/persister/persister.h"
#include "common/logger/logger.h"
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <shared_mutex>
#include <tuple>

FileWriter::FileWriter(const fs::path &path)
    : _temp_path(path.string() + ".tmp"),
     _final_path(path)
{
    _file.open(_temp_path, std::ios::binary); // 二进制模式打开
    if (!_file)
    {
        LOG_FATAL("open persister file {} failed.", _temp_path.string());
        exit(EXIT_FAILURE);
    }
}

FileWriter::~FileWriter()
{
    if (_file)
    {
        _file.close();
        fs::rename(_temp_path, _final_path); // 完成了就把 tmp 名称更改
    }
    else
    {
        // 文件打开失败，但是仍然可能会创建临时文件
        // 因为文件是先创建再打开的，比如权限不足时，可能会创建成功但是打开失败
        fs::remove(_temp_path);
    }
}

void FileWriter::write_string(const std::string &str)
{
    uint32_t len = static_cast<uint32_t>(str.size());
    write(len);
    _file.write(str.data(), len);
}

void FileWriter::write_bytes(const std::vector<uint8_t> &data)
{
    uint32_t len = static_cast<uint32_t>(data.size());
    write(len);
    _file.write(reinterpret_cast<const char*>(data.data()), len);
}

bool FileWriter::is_good()
{
    return _file.good(); // good() = true 表示没有错误标识，即操作都正确
}




Persister::Persister(const std::string &state_file, 
        const std::string &snapshot_file)
    : _state_file(state_file),
      _snapshot_file(snapshot_file)
{
    fs::create_directories(_state_file.parent_path());
}

Persister::~Persister()
{
}

bool Persister::save_state(uint64_t current_term, uint32_t voted_for, const std::vector<uint8_t> &log_data)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    try {
        FileWriter writer(_state_file);
        writer.write(current_term);
        writer.write(voted_for);
        writer.write_bytes(log_data);

        if (!writer.is_good())
        {
            LOG_ERROR("saving state => currrent_term={}, voted_for={} failed.", current_term, voted_for);
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("saving state => currrent_term={}, voted_for={} failed.", current_term, voted_for);
        return false;
    }
}

std::optional<std::tuple<uint64_t, uint32_t, std::vector<uint8_t>>> Persister::load_state()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::ifstream file(_state_file, std::ios::binary);
    if (!file)
    {
        LOG_ERROR("open file: {} failed.", _state_file.string());
        return std::nullopt;
    }

    try {
        uint64_t current_term;
        uint32_t voted_for;
        file.read(reinterpret_cast<char*>(&current_term), sizeof(current_term));
        file.read(reinterpret_cast<char*>(&voted_for), sizeof(voted_for));

        uint32_t log_size;
        file.read(reinterpret_cast<char*>(&log_size), sizeof(log_size));

        std::vector<uint8_t> log_data(log_size);
        file.read(reinterpret_cast<char*>(log_data.data()), log_size);

        if (!file) {
            return std::nullopt;
        }
        return std::make_tuple(current_term, voted_for, std::move(log_data));
    } catch (const std::exception &e) {
        LOG_ERROR("read persistence state: {} failed.", _state_file.string());
        return std::nullopt;
    }
}

bool Persister::save_snapshot(const std::vector<uint8_t> &snapshot_data)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    try {
        FileWriter writer(_snapshot_file);
        writer.write_bytes(snapshot_data);

        if (!writer.is_good())
        {
            LOG_ERROR("saving snapshot failed.");
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        LOG_ERROR("saving snapshot failed.");
        return false;
    }
}

std::optional<std::vector<uint8_t>> Persister::load_snapshot()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::ifstream file(_snapshot_file, std::ios::binary);
    if (!file)
    {
        LOG_ERROR("open file: {} failed.", _snapshot_file.string());
        return std::nullopt;
    }

    try {
        uint32_t snapshot_size;
        file.read(reinterpret_cast<char*>(&snapshot_size), sizeof(snapshot_size));

        std::vector<uint8_t> snapshot_data(snapshot_size);
        file.read(reinterpret_cast<char*>(snapshot_data.data()), snapshot_size);

        if (!file) {
            return std::nullopt;
        }
        return std::move(snapshot_data);
    } catch (const std::exception &e) {
        LOG_ERROR("read persistence state: {} failed.", _snapshot_file.string());
        return std::nullopt;
    }
}