#include "kvserver/kvserver.h"
#include "common/logger/logger.h"
#include "common/persister/persister.h"
#include "common/rpc/raft_rpc_channel.h"
#include "common/rpc/rpc_provider.h"
#include "common/storage/rocksdb_client.h"
#include "discovery/zookeeper/zk_client.h"
#include "kvservice/kvservice.h"
#include "storage/raft/raft_client.h"
#include "storage/raft/state_mathine.h"
#include <algorithm>
#include <cstdint>
#include <google/protobuf/service.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

KVServer::KVServer(const std::string& ip, uint16_t port, const std::string& zk_ip, uint16_t zk_port,
        const std::string& db_path, uint64_t max_raft_logs)
    : _ip(ip), _port(port),
      _zk_ip(zk_ip), _zk_port(zk_port),
      _db_path(db_path), _is_dead(false)
{
    _name = ip + ":" + std::to_string(port);

    // 组件
    _persister = std::make_shared<Persister>();
    _db = std::make_shared<RocksDBClient>(db_path);
    _zk_conn = std::make_shared<ZkClient>();

    // 服务层
    _service = std::make_shared<KVService>(_db, _persister, _peers_conns, _name);

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