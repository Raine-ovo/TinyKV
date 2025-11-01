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
    fs::create_directories(_temp_path.parent_path());

    _file.open(_temp_path, std::ios::binary | std::ios::trunc); // 二进制模式打开
    if (!_file)
    {
        LOG_FATAL("open persister file {} failed.", _temp_path.string());
        exit(EXIT_FAILURE);
    }
}

FileWriter::~FileWriter()
{
    if (_file.is_open())
    {
        _file.close();
        if (fs::exists(_temp_path) && fs::file_size(_temp_path) > 0)
        {
            fs::rename(_temp_path, _final_path); // 完成了就把 tmp 名称更改
            LOG_DEBUG("Successfully persisted data to {}", _final_path.string());
        }
        else
        {
            fs::remove(_temp_path);
        }
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

void FileWriter::write_raw(const void* data, size_t size)
{
    _file.write(reinterpret_cast<const char*>(data), size);
}

bool FileWriter::is_good()
{
    return _file.good(); // good() = true 表示没有错误标识，即操作都正确
}

void FileWriter::flush()
{
    if (_file) _file.flush(); // 刷到磁盘
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
        // writer.write(current_term);
        // writer.write(voted_for);
        // writer.write_bytes(log_data);
        writer.write_raw(&current_term, sizeof(current_term));
        writer.write_raw(&voted_for, sizeof(voted_for));

        uint32_t log_size = static_cast<uint32_t>(log_data.size());
        writer.write_raw(&log_size, sizeof(log_size));
        if (!log_data.empty())
        {
            writer.write_raw(log_data.data(), log_data.size());
        }

        writer.flush(); // 强制刷新到磁盘

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
        LOG_ERROR("load_state => open file: {} failed.", _state_file.string());
        return std::nullopt;
    }

    try {
        uint64_t current_term;
        uint32_t voted_for;
        uint32_t log_size;
        file.read(reinterpret_cast<char*>(&current_term), sizeof(current_term));
        file.read(reinterpret_cast<char*>(&voted_for), sizeof(voted_for));
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

        uint32_t snapshot_size = static_cast<uint32_t>(snapshot_data.size());
        writer.write_raw(&snapshot_size, sizeof(snapshot_size));
        if (!snapshot_data.empty())
        {
            writer.write_raw(snapshot_data.data(), snapshot_data.size());
        }
        
        // writer.write_bytes(snapshot_data);
        writer.flush();

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