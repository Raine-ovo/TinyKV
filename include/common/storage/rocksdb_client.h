#ifndef ROCKSDB_CLIENT_H
#define ROCKSDB_CLIENT_H

#include <string>
#include <vector>

#include <rocksdb/db.h>

class RocksDBClient
{
public:
    RocksDBClient() = delete;
    RocksDBClient(const std::string& path);
    ~RocksDBClient();

    // 对数据库操作
    bool Put(const std::string& key, const std::string& value);
    bool Get(const std::string& key, std::string& value);
    bool Delete(const std::string& key);
    bool PutBatch(const std::vector<std::pair<std::string, std::string>>& kvs);

    bool Exists(const std::string& key);

private:
    bool check_if_exists();

private:
    rocksdb::DB* _db;
};

#endif // ROCKSDB_CLIENT_H