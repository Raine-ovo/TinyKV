#include "common/storage/rocksdb_client.h"
#include "common/logger/logger.h"

#include <rocksdb/options.h>
#include <rocksdb/status.h>
#include <rocksdb/write_batch.h>
#include <filesystem>
namespace fs = std::filesystem;

RocksDBClient::RocksDBClient(const std::string& path)
{
    // 先创建对应的文件夹
    fs::create_directories(fs::path(path).parent_path());
    
    // 配置数据库行为参数
    rocksdb::Options options;
    // 如果指定路径的数据库不存在，则自动创建一个新的数据库
    options.create_if_missing = true;

    // rocksdb 的数据库操作接口大部分都是
    //  Status rocksdb::DB::func(options ...) 形式
    rocksdb::Status status = rocksdb::DB::Open(options, path, &_db);
    if (!status.ok()) 
    {
        LOG_ERROR("打开 RocksDB 数据库失败");
        _db = nullptr;
    }
}

RocksDBClient::~RocksDBClient()
{
    if (_db != nullptr)
    {
        delete _db;
        _db = nullptr;
    }
}

bool RocksDBClient::check_if_exists()
{
    if (_db == nullptr)
    {
        LOG_ERROR("数据库不存在, DB = nullptr");
        return false;
    }

    return true;
}

// 对数据库操作
bool RocksDBClient::Put(const std::string& key, const std::string& value)
{
    if (!check_if_exists()) return false;

    rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), key, value);
    return status.ok();
}

bool RocksDBClient::Get(const std::string& key, std::string &value)
{
    if (!check_if_exists()) return false;

    rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), key, &value);
    return status.ok();
}

bool RocksDBClient::Delete(const std::string& key)
{
    if (!check_if_exists()) return false;

    rocksdb::Status status = _db->Delete(rocksdb::WriteOptions(), key);
    return status.ok();
}

bool RocksDBClient::PutBatch(const std::vector<std::pair<std::string, std::string>>& kvs)
{
    if (!check_if_exists()) return false;

    rocksdb::WriteBatch batch;
    for (const auto& [k, v]: kvs)
    {
        batch.Put(k, v);
    }

    rocksdb::Status status = _db->Write(rocksdb::WriteOptions(), &batch);
    return status.ok();
}

bool RocksDBClient::Exists(const std::string& key)
{
    std::string value;
    return Get(key, value);
}