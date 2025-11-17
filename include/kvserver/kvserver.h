#ifndef KVSERVER_H
#define KVSERVER_H

#include "common/persister/persister.h"
#include "common/storage/rocksdb_client.h"
#include "discovery/zookeeper/zk_client.h"
#include "kvservice/kvservice_interface.h"
#include "storage/raft/raft_client.h"
#include "kvservice/kvservice.h"
#include "storage/raft/state_mathine.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <variant>
#include <vector>

// kvserver 实现网络层+组件管理

class KVServer : std::enable_shared_from_this<KVServer>
{
public:
    KVServer(const std::string& ip, uint16_t port, const std::string& zk_ip, uint16_t zk_port,
            const std::string& db_path, uint64_t max_raft_logs);
    ~KVServer();

    void Start();
    void Kill();
    bool Killed();

    // 获取当前服务器的raft节点编号
    uint32_t GetMyId() const { return _me; }

private:
    void connectPeers(const std::vector<std::string>& servers);
    void childWatcher(); // zookeeper 子节点变化监听

private:
    // 网络层
    std::string _ip;
    uint16_t _port;
    std::string _zk_ip;
    uint16_t _zk_port;
    std::string _name;

    std::string _db_path;
    uint64_t _max_raft_logs;

    std::atomic<bool> _is_dead;

    // 组件
    std::shared_ptr<Persister> _persister;
    std::shared_ptr<KVService> _service; // 服务层
    std::shared_ptr<RocksDBClient> _db; // 数据库
    std::shared_ptr<ZkClient> _zk_conn; // zk 客户端

    // raft 相关
    std::shared_ptr<KVStateMathine> _sm;
    std::mutex _peer_mutex;
    std::vector<std::shared_ptr<RaftClient>> _peers_conns;
    std::string _zk_servers_path;
    uint32_t _me; // raft 层编号
};

#endif