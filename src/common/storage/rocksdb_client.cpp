#include "common/storage/rocksdb_client.h"
#include "common/logger/logger.h"
#include <functional>
#include "common/logger/logger.h"
#include "proto/command.pb.h"

#include <memory>
#include <mutex>
#include <rocksdb/iterator.h>
#include <rocksdb/options.h>
#include <rocksdb/status.h>
#include <rocksdb/write_batch.h>
#include <filesystem>
#include <shared_mutex>
#include <unordered_map>
namespace fs = std::filesystem;

RocksDBClient::RocksDBClient(const std::string& path)
{
    // 先创建对应的文件夹
    fs::create_directories(fs::path(path).parent_path());
    
    // 配置数据库行为参数
    // 如果指定路径的数据库不存在，则自动创建一个新的数据库
    _options.create_if_missing = true;
    // 没有对应的列族就创建
    _options.create_missing_column_families = true;

    _cf_descs.emplace_back("raft_meta_cf", rocksdb::ColumnFamilyOptions());
    _cf_descs.emplace_back("kv_cf", rocksdb::ColumnFamilyOptions());
    _cf_descs.emplace_back("client_cf", rocksdb::ColumnFamilyOptions());

    _db_path = path;

    // rocksdb 的数据库操作接口大部分都是
    //  Status rocksdb::DB::func(options ...) 形式
    rocksdb::Status status = rocksdb::DB::Open(_options, path, _cf_descs, &_cf_handles, &_db);
    if (!status.ok()) 
    {
        LOG_ERROR("打开 RocksDB 数据库失败: {}", status.ToString());
        _db = nullptr;
    }
    else
    {
        // 把创建的列族单独保存下来
        for (auto handle : _cf_handles)
        {
            if (handle->GetName() == "raft_cf")
            {
                _raft_meta_cf = handle;
            }
            else if (handle->GetName() == "kv_cf")
            {
                _kv_cf = handle;
            }
            else if (handle->GetName() == "client_cf")
            {
                _client_cf = handle;
            }
        }
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

// 保存 raft 层的元数据，如 currentTerm、votedFor、logs 等
bool RocksDBClient::raftMetaPut(const std::string& key, const std::string& value)
{
    std::unique_lock<std::shared_mutex> lock(_raft_meta_mutex);
    if (!_db || !_raft_meta_cf)
    {
        LOG_ERROR("raft 层写入元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), _raft_meta_cf, key, value);
    if (!status.ok())
    {
        LOG_ERROR("raft 层写入元数据失败: {}", status.ToString());
        return false;
    }
    return true;
}

// 获取 raft 层的元数据
bool RocksDBClient::raftMetaGet(const std::string& key, std::string& value)
{
    std::shared_lock<std::shared_mutex> lock(_raft_meta_mutex);
    if (!_db || !_raft_meta_cf)
    {
        LOG_ERROR("raft 层读取元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), _raft_meta_cf, key, &value);
    if (!status.ok())
    {
        if (status.IsNotFound())
        {
            value = "";
            return true;
        }
        else
        {
            LOG_ERROR("raft 层读取元数据失败: {}", status.ToString());
            return false;
        }
    }
    return true;
}

// 删除 raft 层的元数据
bool RocksDBClient::raftMetaDelete(const std::string& key)
{
    std::unique_lock<std::shared_mutex> lock(_raft_meta_mutex);
    if (!_db || !_raft_meta_cf)
    {
        LOG_ERROR("raft 层删除元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }
    rocksdb::Status status = _db->Delete(rocksdb::WriteOptions(), _raft_meta_cf, key);
    if (!status.ok())
    {
        LOG_ERROR("raft 层删除元数据失败: {}", status.ToString());
        return false;
    }
    return true;
}

// 保存 kv 层数据
::command::PutReply RocksDBClient::KVPut(const std::string& key, const std::string& value)
{
    std::unique_lock<std::shared_mutex> lock(_kv_mutex);
    ::command::PutReply reply;
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("KV 层写入元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        reply.set_err(command::StateCode::ErrDBError);
        return reply;
    }
    rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), _kv_cf, key, value);
    if (!status.ok())
    {
        LOG_ERROR("KV 层写入元数据失败: {}", status.ToString());
        reply.set_err(command::StateCode::ErrDBError);
        return reply;
    }
    reply.set_err(::command::StateCode::OK);
    return reply;
}

// 获取 kv 层数据
::command::GetReply RocksDBClient::KVGet(const std::string& key, std::string& value)
{
    std::shared_lock<std::shared_mutex> lock(_kv_mutex);
    ::command::GetReply reply;
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("KV 层读取元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        reply.set_err(command::StateCode::ErrDBError);
        return reply;
    }
    rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), _kv_cf, key, &value);
    if (!status.ok())
    {
        if (status.IsNotFound())
        {
            reply.set_err(::command::StateCode::ErrNoKey);
            value = "";
            return reply;
        }
        else
        {
            LOG_ERROR("KV 层读取元数据失败: {}", status.ToString());
            reply.set_err(command::StateCode::ErrDBError);
            return reply;
        }
    }
    reply.set_err(command::StateCode::OK);
    reply.set_value(value);
    return reply;
}

// 删除 kv 层数据
::command::DeleteReply RocksDBClient::KVDelete(const std::string& key)
{
    std::unique_lock<std::shared_mutex> lock(_kv_mutex);
    ::command::DeleteReply reply;
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("KV 层删除元数据失败, {} >> {} >> {}", __FILE__, __FUNCTION__, __LINE__);
        reply.set_err(command::StateCode::ErrDBError);
        return reply;
    }
    rocksdb::Status status = _db->Delete(rocksdb::WriteOptions(), _kv_cf, key);
    if (!status.ok())
    {
        LOG_ERROR("KV 层删除元数据失败: {}", status.ToString());
        reply.set_err(command::StateCode::ErrDBError);
        return reply;
    }
    reply.set_err(::command::StateCode::OK);
    return reply;
}

::command::AppendReply RocksDBClient::KVAppend(const std::string& key, const std::string& append_value)
{
    ::command::AppendReply reply;

    std::string value;
    auto getreply = KVGet(key, value);
    if (getreply.err() != ::command::StateCode::OK)
    {
        reply.set_err(getreply.err());
        return reply;
    }
    
    std::string new_value = value + append_value;
    auto putreply = KVPut(key, new_value);
    if (putreply.err() != ::command::StateCode::OK)
    {
        reply.set_err(putreply.err());
        return reply;
    }
    
    reply.set_err(::command::StateCode::OK);
    reply.set_new_value(new_value);
    return reply;
}

std::unordered_map<std::string, std::string> RocksDBClient::GenerateKVSnapshot()
{
    std::shared_lock<std::shared_mutex> lock(_kv_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("{}>>{}>>{}: KV 快照生成失败，列族不存在或数据库没有初始化.", __FILE__, __FUNCTION__, __LINE__);
        return {};
    }
    std::unordered_map<std::string, std::string> snapshot;
    rocksdb::Iterator* iter = _db->NewIterator(rocksdb::ReadOptions(), _kv_cf);
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
    {
        snapshot[iter->key().ToString()] = iter->value().ToString();
    }
    return snapshot;
}

bool RocksDBClient::InstallKVSnapshot(std::unordered_map<std::string, std::string>& kv_snapshot)
{
    std::unique_lock<std::shared_mutex> lock(_kv_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("{}>>{}>>{}: KV 快照安装失败，列族不存在或数据库没有初始化.", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }

    // 删除列族，重新创建一个
    for (int i = 0; i < _cf_handles.size(); i ++ )
    {
        if (_cf_handles[i]->GetName() == "kv_cf")
        {
            _cf_handles.erase(_cf_handles.begin() + i);
            break;
        }
    }

    _db->DropColumnFamily(_kv_cf);
    _kv_cf = nullptr;

    _db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), "kv_cf", &_kv_cf);
    _cf_handles.push_back(_kv_cf);

    // 在新的列族安装快照
    for (const auto &[k, v]: kv_snapshot)
    {
        KVPut(k, v);
    }
    return true;
}

// 保存客户端操作数据
::command::PutReply RocksDBClient::ClientPut(const std::string& key, const std::string& value)
{
    std::unique_lock<std::shared_mutex> lock(_client_mutex);
    
    ::command::PutReply reply;
    if (!_db || !_client_cf)
    {
        LOG_ERROR("写入客户端操作失败，数据库未打开或列簇不存在");
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }

    rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), _client_cf, key, value);
    if (!status.ok())
    {
        LOG_ERROR("写入客户端操作失败: {}", status.ToString());
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }

    reply.set_err(::command::StateCode::OK);
    return reply;
}

// 获取客户端操作数据
::command::GetReply RocksDBClient::ClientGet(const std::string& key, std::string& value)
{
    std::shared_lock<std::shared_mutex> lock(_client_mutex);

    ::command::GetReply reply;
    if (!_db || !_client_cf)
    {
        LOG_ERROR("获取客户端操作数据失败: 数据库未打开或列簇不存在");
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }

    rocksdb::Status status = _db->Get(rocksdb::ReadOptions(), _client_cf, key, &value);
    if (!status.ok())
    {
        LOG_ERROR("获取客户端操作数据失败：{}", status.ToString());
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }

    reply.set_err(::command::StateCode::OK);
    reply.set_value(value);
    return reply;
}

// 删除客户端操作数据
::command::DeleteReply RocksDBClient::ClientDelete(const std::string& key)
{
    std::unique_lock<std::shared_mutex> lock(_client_mutex);

    ::command::DeleteReply reply;
    if (!_db || !_client_cf)
    {
        LOG_ERROR("删除客户端操作数据失败: 数据库未打开或列簇不存在");
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }
    
    rocksdb::Status status = _db->Delete(rocksdb::WriteOptions(), _client_cf, key);
    if (!status.ok())
    {
        LOG_ERROR("删除客户端操作数据失败：{}", status.ToString());
        reply.set_err(::command::StateCode::ErrDBError);
        return reply;
    }

    reply.set_err(::command::StateCode::OK);
    return reply;
}

// 获取客户端操作快照
std::unordered_map<std::string, std::string> RocksDBClient::GenerateClientSnapshot()
{
    std::shared_lock<std::shared_mutex> lock(_client_mutex);
    
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("{}>>{}>>{}: 客户端操作快照生成失败，列族不存在或数据库没有初始化.", __FILE__, __FUNCTION__, __LINE__);
        return {};
    }

    std::unordered_map<std::string, std::string> snapshot;
    rocksdb::Iterator* iter = _db->NewIterator(rocksdb::ReadOptions(), _client_cf);
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
    {
        snapshot[iter->key().ToString()] = iter->value().ToString();
    }
    return snapshot;
}

// 安装客户端操作快照
bool RocksDBClient::InstallClientSnapshot(std::unordered_map<std::string, std::string>& client_snapshot)
{
    std::unique_lock<std::shared_mutex> lock(_client_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("{}>>{}>>{}: 客户端操作快照安装失败，列族不存在或数据库没有初始化.", __FILE__, __FUNCTION__, __LINE__);
        return false;
    }

    // 删除列簇并重新创建
    for (int i = 0; i < _cf_handles.size(); i ++ )
    {
        if (_cf_handles[i]->GetName() == "client_cf")
        {
            _cf_handles.erase(_cf_handles.begin() + i);
            break;
        }
    }

    _db->DropColumnFamily(_client_cf);
    _client_cf = nullptr;

    _db->CreateColumnFamily(rocksdb::ColumnFamilyOptions(), "client_cf", &_client_cf);
    _cf_handles.push_back(_client_cf);

    for (const auto& [k, v]: client_snapshot)
    {
        ClientPut(k, v);
    }
    return true;
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

// 导出指定分片的数据
std::unordered_map<std::string, std::string> RocksDBClient::ExportShardData(
    uint32_t shard_id,
    std::function<uint32_t(const std::string&)> get_shard_func)
{
    std::shared_lock<std::shared_mutex> lock(_kv_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("导出分片数据失败：列族不存在或数据库没有初始化");
        return {};
    }

    std::unordered_map<std::string, std::string> shard_data;
    rocksdb::Iterator* iter = _db->NewIterator(rocksdb::ReadOptions(), _kv_cf);
    
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
    {
        std::string key = iter->key().ToString();
        // 计算key所属的分片
        uint32_t key_shard = get_shard_func(key);
        
        // 只导出属于指定分片的数据
        if (key_shard == shard_id)
        {
            shard_data[key] = iter->value().ToString();
        }
    }
    
    delete iter;
    LOG_INFO("导出分片{}的数据，共{}条记录", shard_id, shard_data.size());
    return shard_data;
}

// 导入分片数据（批量写入）
bool RocksDBClient::ImportShardData(const std::unordered_map<std::string, std::string>& data)
{
    std::unique_lock<std::shared_mutex> lock(_kv_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("导入分片数据失败：列族不存在或数据库没有初始化");
        return false;
    }

    // 使用批量写入提高性能
    rocksdb::WriteBatch batch;
    for (const auto& [key, value] : data)
    {
        batch.Put(_kv_cf, key, value);
    }

    rocksdb::Status status = _db->Write(rocksdb::WriteOptions(), &batch);
    if (!status.ok())
    {
        LOG_ERROR("导入分片数据失败: {}", status.ToString());
        return false;
    }

    LOG_INFO("导入分片数据成功，共{}条记录", data.size());
    return true;
}

// 删除指定分片的所有数据
bool RocksDBClient::DeleteShardData(
    uint32_t shard_id,
    std::function<uint32_t(const std::string&)> get_shard_func)
{
    std::unique_lock<std::shared_mutex> lock(_kv_mutex);
    if (!_db || !_kv_cf)
    {
        LOG_ERROR("删除分片数据失败：列族不存在或数据库没有初始化");
        return false;
    }

    // 先找出所有属于该分片的key
    std::vector<std::string> keys_to_delete;
    rocksdb::Iterator* iter = _db->NewIterator(rocksdb::ReadOptions(), _kv_cf);
    
    for (iter->SeekToFirst(); iter->Valid(); iter->Next())
    {
        std::string key = iter->key().ToString();
        uint32_t key_shard = get_shard_func(key);
        
        if (key_shard == shard_id)
        {
            keys_to_delete.push_back(key);
        }
    }
    delete iter;

    // 批量删除
    if (!keys_to_delete.empty())
    {
        rocksdb::WriteBatch batch;
        for (const auto& key : keys_to_delete)
        {
            batch.Delete(_kv_cf, key);
        }

        rocksdb::Status status = _db->Write(rocksdb::WriteOptions(), &batch);
        if (!status.ok())
        {
            LOG_ERROR("删除分片数据失败: {}", status.ToString());
            return false;
        }
    }

    LOG_INFO("删除分片{}的数据，共{}条记录", shard_id, keys_to_delete.size());
    return true;
}