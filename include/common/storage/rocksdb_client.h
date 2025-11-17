#ifndef ROCKSDB_CLIENT_H
#define ROCKSDB_CLIENT_H

#include "proto/command.pb.h"
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rocksdb/db.h>

class RocksDBClient
{
public:
    RocksDBClient() = delete;
    RocksDBClient(const std::string& path);
    ~RocksDBClient();

    // 保存 raft 层的元数据，如 currentTerm、votedFor、logs 等
    bool raftMetaPut(const std::string& key, const std::string& value);
    // 获取 raft 层的元数据
    bool raftMetaGet(const std::string& key, std::string& value);
    // 删除 raft 层的元数据
    bool raftMetaDelete(const std::string& key);

    // 保存 kv 层数据
    ::command::PutReply KVPut(const std::string& key, const std::string& value);
    // 获取 kv 层数据
    ::command::GetReply KVGet(const std::string& key, std::string& value);
    // 删除 kv 层数据
    ::command::DeleteReply KVDelete(const std::string& key);
    // append kv 数据
    ::command::AppendReply KVAppend(const std::string& key, const std::string& append_value);
    // 获取 KV 层快照
    std::unordered_map<std::string, std::string> GenerateKVSnapshot();
    // 将快照安装在 KV 层
    bool InstallKVSnapshot(std::unordered_map<std::string, std::string>& kv_snapshot);
    
    // 保存客户端操作数据
    ::command::PutReply ClientPut(const std::string& key, const std::string& value);
    // 获取客户端操作数据
    ::command::GetReply ClientGet(const std::string& key, std::string& value);
    // 删除客户端操作数据
    ::command::DeleteReply ClientDelete(const std::string& key);
    // 获取客户端操作快照
    std::unordered_map<std::string, std::string> GenerateClientSnapshot();
    // 安装客户端操作快照
    bool InstallClientSnapshot(std::unordered_map<std::string, std::string>& client_snapshot);
    


    // 对数据库操作
    bool Put(const std::string& key, const std::string& value);
    bool Get(const std::string& key, std::string& value);
    bool Delete(const std::string& key);
    bool PutBatch(const std::vector<std::pair<std::string, std::string>>& kvs);

    bool Exists(const std::string& key);

private:
    bool check_if_exists();

private:
    // 数据库的路径
    std::string _db_path;

    // 数据库的选项
    rocksdb::Options _options;
    // 数据库指针
    rocksdb::DB* _db;

    // 列族句柄，用于对列族进行操作
    /* 列族是 rocksdb 中的一种概念，用于将数据进行分类存储
        类似于 mysql 中的表
        这里我们使用两个列族来保存数据
        - raft_meta: 保存 raft 层的元数据
        - kv: 保存 kv 层的数据

        https://www.cnblogs.com/dennis-wong/p/16027863.html
    */ 
    std::vector<rocksdb::ColumnFamilyDescriptor> _cf_descs;
    std::vector<rocksdb::ColumnFamilyHandle*> _cf_handles;

    // 对 raft 层进行操作的锁
    std::shared_mutex _raft_meta_mutex;
    // 保存 raft 层数据的列族
    rocksdb::ColumnFamilyHandle* _raft_meta_cf;

    // 对 kv 层进行操作的锁
    std::shared_mutex _kv_mutex;
    // 保存 kv 层数据的列族
    rocksdb::ColumnFamilyHandle* _kv_cf;

    // 对客户端操作记录进行操作的锁
    std::shared_mutex _client_mutex;
    rocksdb::ColumnFamilyHandle* _client_cf;

};

#endif // ROCKSDB_CLIENT_H