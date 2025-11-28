#include "kvserver/kvserver.h"
#include "common/logger/logger.h"
#include "common/persister/persister.h"
#include "common/rpc/raft_rpc_channel.h"
#include "common/rpc/rpc_provider.h"
#include "common/storage/rocksdb_client.h"
#include "discovery/zookeeper/zk_client.h"
#include "kvservice/kv_client.h"
#include "kvservice/kvservice.h"
#include "proto/command.pb.h"
#include "storage/raft/raft_client.h"
#include "storage/raft/state_mathine.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <google/protobuf/service.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

KVServer::KVServer(const std::string& ip, uint16_t port, const std::string& zk_ip, uint16_t zk_port,
        const std::string& db_path, uint64_t max_raft_logs, uint32_t num_shards, uint32_t replica_count)
    : _ip(ip), _port(port),
      _zk_ip(zk_ip), _zk_port(zk_port),
      _db_path(db_path), _is_dead(false), _num_shards(num_shards)
{
    _name = ip + ":" + std::to_string(port);

    // 组件
    _persister = std::make_shared<Persister>();
    _db = std::make_shared<RocksDBClient>(db_path);
    _zk_conn = std::make_shared<ZkClient>();

    // 分片管理器（如果启用）
    if (_num_shards > 0) {
        // 默认使用3个副本（推荐奇数，便于Raft形成多数派）
        if (replica_count == 0) {
            replica_count = 3;
        }
        _shard_manager = std::make_shared<ShardManager>(_num_shards, replica_count);
    }

    // 服务层
    _service = std::make_shared<KVService>(_db, _persister, _peers_conns, nullptr, _name, _shard_manager);

    _zk_servers_path = "/kvserver/servers";
}

KVServer::~KVServer()
{
    Kill();
}

void KVServer::Start()
{
    // 连接 zookeeper
    _zk_conn->connect();
    // 注册服务器根节点
    _zk_conn->CreatePersistentNode(_zk_servers_path, "");
    // 注册服务器
    std::string node_path = _zk_servers_path + "/" + _name;
    _zk_conn->CreateEphemeralNode(node_path, _name);
    // 注册子节点监听器
    _zk_conn->RegisterChildWatcher(_zk_servers_path, std::bind(&KVServer::childWatcher, this));

    // 获取服务器列表
    std::vector<std::string> servers;
    _zk_conn->GetChildren(_zk_servers_path, servers);

    // 连接到对等节点
    connectPeers(servers);
    
    // 如果启用了分片，更新分片映射
    if (_shard_manager && !servers.empty()) {
        std::vector<std::string> node_names;
        for (const auto& server : servers) {
            std::string peer_info;
            _zk_conn->GetNodeData(_zk_servers_path + "/" + server, peer_info);
            if (!peer_info.empty()) {
                node_names.push_back(peer_info);
            }
        }
        _shard_manager->UpdateNodes(node_names);
        _service->UpdateNodes(node_names);
    }

    // 开启 rpc 服务
    std::thread rpc_td([this]() {
        auto provider = std::make_shared<RpcProvider>(_ip, _port);
        provider->registerService(_service.get());
        provider->run();
    });
    // 分离主线程运行
    rpc_td.detach();

    LOG_INFO("KVServer{} started successfully", _name);
}

void KVServer::connectPeers(const std::vector<std::string>& servers)
{
    std::vector<std::shared_ptr<RaftClient>> new_peers;
    /*
        由于 zookeeper 的 child 返回不按照顺序，因此每次 servers 的获取顺序都可能不同
        这里使用排序确定自己的编号
    */
    std::stable_sort(_peers_conns.begin(), _peers_conns.end());
    for (int i = 0; i < servers.size(); ++ i)
    {
        const auto& server = servers[i];
        std::string peer_info;
        _zk_conn->GetNodeData(_zk_servers_path + "/" + server, peer_info);
        
        if (peer_info == _name)
        {
            new_peers.push_back(nullptr);
            if (_sm == nullptr)
            {
                _me = i;
                // 创建状态机
                _sm = std::make_shared<KVStateMathine>();
                _sm->Make(_peers_conns, _me, _persister, _service);
                // 设置配置变更回调，当配置变更被应用时自动更新连接
                _sm->SetConfigChangeCallback(
                    std::bind(&KVServer::handleConfigChange, this, std::placeholders::_1));
                // 更新service的状态机引用
                _service->SetStateMachine(_sm);
                if (_shard_manager) {
                    _service->SetShardManager(_shard_manager);
                }
            }
            else
            {
                _sm->UpdatePeers(_peers_conns);
            }
            continue;
        }

        int pos = peer_info.find(":");
        std::string peer_ip = peer_info.substr(0, pos);
        uint16_t peer_port = std::stoi(peer_info.substr(pos + 1));

        auto peer_conn = std::make_shared<RaftClient>(new class RaftRpcChannel(peer_ip, peer_port));
        new_peers.push_back(peer_conn);

        LOG_INFO("Server{} connected to peer: {}", _name, peer_info);
    }

    std::lock_guard<std::mutex> lock(_peer_mutex);
    _peers_conns = std::move(new_peers);
}

void KVServer::childWatcher()
{
    LOG_INFO("Server{} detected peer list change", _name);

    std::vector<std::string> servers;
    if (_zk_conn->GetChildren(_zk_servers_path, servers))
    {
        connectPeers(servers);
        
        // 如果启用了分片，更新分片映射
        if (_shard_manager && !servers.empty()) {
            std::vector<std::string> node_names;
            for (const auto& server : servers) {
                std::string peer_info;
                _zk_conn->GetNodeData(_zk_servers_path + "/" + server, peer_info);
                if (!peer_info.empty()) {
                    node_names.push_back(peer_info);
                }
            }
            _shard_manager->UpdateNodes(node_names);
            _service->UpdateNodes(node_names);
        }
    }
}

void KVServer::handleConfigChange(const ::command::ConfigChangeCommand& config_change)
{
    LOG_INFO("KVServer{} 处理配置变更: type={}, peer_id={}", 
        _name,
        config_change.change_type() == ::command::ConfigChangeCommand::ADD_PEER ? "ADD" : "REMOVE",
        config_change.peer_id());

    if (config_change.change_type() == ::command::ConfigChangeCommand::ADD_PEER)
    {
        // 添加节点：创建新的RaftClient连接
        std::string address = config_change.peer_address();
        if (address.empty())
        {
            LOG_ERROR("KVServer{} 添加节点失败：地址为空", _name);
            return;
        }

        int pos = address.find(":");
        if (pos == std::string::npos)
        {
            LOG_ERROR("KVServer{} 添加节点失败：地址格式错误: {}", _name, address);
            return;
        }

        std::string peer_ip = address.substr(0, pos);
        uint16_t peer_port = static_cast<uint16_t>(std::stoi(address.substr(pos + 1)));

        // 创建新的RaftClient连接
        auto peer_conn = std::make_shared<RaftClient>(new class RaftRpcChannel(peer_ip, peer_port));
        
        {
            std::lock_guard<std::mutex> lock(_peer_mutex);
            
            // 确保peers_conns数组足够大
            uint32_t peer_id = config_change.peer_id();
            if (peer_id >= _peers_conns.size())
            {
                _peers_conns.resize(peer_id + 1, nullptr);
            }
            
            // 更新连接
            _peers_conns[peer_id] = peer_conn;
            
            LOG_INFO("KVServer{} 添加节点连接: peer_id={}, address={}", _name, peer_id, address);
        }

        // 更新Raft的peers列表
        if (_sm)
        {
            std::lock_guard<std::mutex> lock(_peer_mutex);
            _sm->UpdatePeers(_peers_conns);
        }
    }
    else if (config_change.change_type() == ::command::ConfigChangeCommand::REMOVE_PEER)
    {
        // 移除节点：将连接设置为nullptr
        uint32_t peer_id = config_change.peer_id();
        
        {
            std::lock_guard<std::mutex> lock(_peer_mutex);
            
            if (peer_id < _peers_conns.size())
            {
                _peers_conns[peer_id] = nullptr;
                LOG_INFO("KVServer{} 移除节点连接: peer_id={}", _name, peer_id);
            }
        }

        // 更新Raft的peers列表
        if (_sm)
        {
            std::lock_guard<std::mutex> lock(_peer_mutex);
            _sm->UpdatePeers(_peers_conns);
        }
    }
}

void KVServer::migrateShards(const std::unordered_map<uint32_t, std::string>& old_shard_leaders,
                              const std::unordered_map<uint32_t, std::string>& new_shard_leaders)
{
    if (!_shard_manager || !_service) {
        return;
    }

    LOG_INFO("开始分片迁移，检查需要迁移的分片...");

    // 找出需要迁移的分片（主节点发生变化）
    std::vector<uint32_t> shards_to_migrate;
    for (const auto& [shard_id, new_leader] : new_shard_leaders) {
        auto it = old_shard_leaders.find(shard_id);
        if (it == old_shard_leaders.end() || it->second != new_leader) {
            // 分片主节点发生变化，需要迁移
            shards_to_migrate.push_back(shard_id);
        }
    }

    if (shards_to_migrate.empty()) {
        LOG_INFO("没有需要迁移的分片");
        return;
    }

    LOG_INFO("需要迁移{}个分片", shards_to_migrate.size());

    // 对每个需要迁移的分片进行处理
    for (uint32_t shard_id : shards_to_migrate) {
        std::string new_leader = new_shard_leaders.at(shard_id);
        auto old_it = old_shard_leaders.find(shard_id);
        std::string old_leader = (old_it != old_shard_leaders.end()) ? old_it->second : "";

        // 如果新主节点是当前节点，需要从旧主节点拉取数据
        if (new_leader == _name && !old_leader.empty() && old_leader != _name) {
            LOG_INFO("节点{}需要从{}拉取分片{}的数据", _name, old_leader, shard_id);
            
            // 创建到旧主节点的客户端
            auto [ip, port] = _service->ParseNodeAddress(old_leader);
            if (ip.empty() || port == 0) {
                LOG_ERROR("无法解析旧主节点地址: {}", old_leader);
                continue;
            }

            auto client = std::make_shared<KVClient>(ip, port);
            
            // 拉取分片数据
            ::command::PullShardCommand pull_request;
            pull_request.set_shard_id(shard_id);
            pull_request.set_config_num(0); // TODO: 使用配置版本号

            auto pull_response = client->PullShard(pull_request);
            
            if (pull_response.err() == ::command::StateCode::OK) {
                // 导入数据
                std::unordered_map<std::string, std::string> shard_data;
                for (const auto& [key, value] : pull_response.data()) {
                    shard_data[key] = value;
                }

                if (!shard_data.empty()) {
                    bool success = _db->ImportShardData(shard_data);
                    if (success) {
                        LOG_INFO("成功导入分片{}的数据，共{}条记录", shard_id, shard_data.size());
                        
                        // 通知旧主节点删除数据（可选，也可以延迟删除）
                        // 这里先不删除，等迁移稳定后再删除
                    } else {
                        LOG_ERROR("导入分片{}的数据失败", shard_id);
                    }
                }
            } else {
                LOG_ERROR("拉取分片{}的数据失败: {}", shard_id, pull_response.err());
            }
        }
        // 如果旧主节点是当前节点，但新主节点不是，可以延迟删除数据
        // 等新主节点确认迁移完成后再删除
    }
}

void KVServer::Kill()
{
    _is_dead = true;
}

bool KVServer::Killed()
{
    return _is_dead;
}