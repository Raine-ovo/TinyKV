#include "kvservice/kvservice.h"
#include "kvservice/kv_client.h"
#include "common/logger/logger.h"
#include "common/shard/shard_manager.h"
#include "proto/command.pb.h"
#include "storage/raft/state_mathine.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "common/boost/serializer.h"

KVService::KVService(std::shared_ptr<RocksDBClient> db,
        std::shared_ptr<Persister> persister,
        std::vector<std::shared_ptr<RaftClient>>& peer_conns,
        std::shared_ptr<KVStateMathine> sm,
        const std::string& name,
        std::shared_ptr<ShardManager> shard_manager)
    : _db(db), _persister(persister), _name(name), _sm(sm), _shard_manager(shard_manager)
{
}


KVService::~KVService()
{
}

// 向 statemachine 暴露的接口
::command::GetReply KVService::Get(::command::GetCommand command)
{
    std::string key = command.key();
    std::string value;

    ::command::GetReply reply = _db->KVGet(key, value);
    return reply;
}

::command::PutReply KVService::Put(::command::PutCommand command)
{
    std::string key = command.key();
    std::string value = command.value();

    ::command::PutReply reply = _db->KVPut(key, value);
    return reply;
}

::command::DeleteReply KVService::Delete(::command::DeleteCommand command)
{
    std::string key = command.key();

    ::command::DeleteReply reply = _db->KVDelete(key);
    return reply;
}

::command::AppendReply KVService::Append(::command::AppendCommand command)
{
    std::string key = command.key();
    std::string value = command.value();

    ::command::AppendReply reply = _db->KVAppend(key, value);
    return reply;
}

std::vector<uint8_t> KVService::snapshot()
{
    std::vector<uint8_t> encode;

    auto all_kvs = _db->GenerateKVSnapshot();
    auto all_kvs_str = Serializer::SerializeToString(all_kvs);
    encode.emplace_back(all_kvs_str.size());
    encode.insert(encode.end(), all_kvs_str.begin(), all_kvs_str.end());

    auto last_request_str = Serializer::SerializeToString(_last_request_ids);
    encode.emplace_back(last_request_str.size());
    encode.insert(encode.end(), last_request_str.begin(), last_request_str.end());

    auto last_replis_str = Serializer::SerializeToString(_last_replis);
    encode.insert(encode.end(), last_replis_str.begin(), last_replis_str.end());

    return encode;
}

void KVService::restore(const std::vector<uint8_t>& snapshot)
{
    // 先解码
    size_t offset = 0;

    size_t all_kvs_len;
    memcpy(&all_kvs_len, snapshot.data(), sizeof(size_t));
    offset += sizeof(size_t);
    std::string all_kvs_str;
    memcpy(all_kvs_str.data(), snapshot.data() + offset, sizeof(all_kvs_len));
    offset += all_kvs_len;
    std::unordered_map<std::string, std::string> all_kvs;
    Serializer::UnserializeFromString(all_kvs, all_kvs_str);
    _db->InstallKVSnapshot(all_kvs);

    _last_replis.clear();
    _last_request_ids.clear();

    size_t last_request_len;
    memcpy(&last_request_len, snapshot.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);
    std::string last_request_str;
    memcpy(last_request_str.data(), snapshot.data() + offset, sizeof(last_request_len));
    offset += sizeof(last_request_len);
    Serializer::UnserializeFromString(_last_request_ids, last_request_str);

    size_t last_replis_len;
    memcpy(&last_replis_len, snapshot.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);
    std::string last_replis_str;
    memcpy(last_replis_str.data(), snapshot.data() + offset, sizeof(last_replis_len));
    offset += sizeof(last_replis_len);
    Serializer::UnserializeFromString(_last_replis, last_replis_str);
}

// rpc 服务接口
void KVService::Get(::google::protobuf::RpcController* controller,
            const ::command::GetCommand* request,
            ::command::GetReply* response,
            ::google::protobuf::Closure* done)
{
    std::string key = request->key();
    
    // 如果启用了分片，检查是否需要转发
    if (_shard_manager && !_shard_manager->IsShardLeader(_shard_manager->GetShard(key), _name)) {
        // 需要转发到主节点
        uint32_t shard_id = _shard_manager->GetShard(key);
        std::string target_node = _shard_manager->GetLeaderForShard(shard_id);
        
        if (target_node.empty()) {
            response->set_err(::command::StateCode::ErrWrongGroup);
            LOG_ERROR("Shard {} has no leader", shard_id);
            done->Run();
            return;
        }
        
        // 转发请求到主节点
        auto client = GetOrCreateClient(target_node);
        *response = client->Get(*request);
        done->Run();
        return;
    }
    
    // 本地处理
    if (!_sm) {
        response->set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("State machine not initialized");
        done->Run();
        return;
    }
    
    ::command::Command command;
    auto cmd = command.mutable_get();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::GetReply>(result);

    done->Run();
}
            
void KVService::Put(::google::protobuf::RpcController* controller,
            const ::command::PutCommand* request,
            ::command::PutReply* response,
            ::google::protobuf::Closure* done)
{
    std::string key = request->key();
    
    // 如果启用了分片，检查是否需要转发
    if (_shard_manager && !_shard_manager->IsShardLeader(_shard_manager->GetShard(key), _name)) {
        // 需要转发到主节点
        uint32_t shard_id = _shard_manager->GetShard(key);
        std::string target_node = _shard_manager->GetLeaderForShard(shard_id);
        
        if (target_node.empty()) {
            response->set_err(::command::StateCode::ErrWrongGroup);
            LOG_ERROR("Shard {} has no leader", shard_id);
            done->Run();
            return;
        }
        
        // 转发请求到主节点
        auto client = GetOrCreateClient(target_node);
        *response = client->Put(*request);
        done->Run();
        return;
    }
    
    // 本地处理
    if (!_sm) {
        response->set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("State machine not initialized");
        done->Run();
        return;
    }
    
    ::command::Command command;
    auto cmd = command.mutable_put();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::PutReply>(result);

    done->Run();
}
            
void KVService::Delete(::google::protobuf::RpcController* controller,
            const ::command::DeleteCommand* request,
            ::command::DeleteReply* response,
            ::google::protobuf::Closure* done)
{
    std::string key = request->key();
    
    // 如果启用了分片，检查是否需要转发
    if (_shard_manager && !_shard_manager->IsShardLeader(_shard_manager->GetShard(key), _name)) {
        // 需要转发到主节点
        uint32_t shard_id = _shard_manager->GetShard(key);
        std::string target_node = _shard_manager->GetLeaderForShard(shard_id);
        
        if (target_node.empty()) {
            response->set_err(::command::StateCode::ErrWrongGroup);
            LOG_ERROR("Shard {} has no leader", shard_id);
            done->Run();
            return;
        }
        
        // 转发请求到主节点
        auto client = GetOrCreateClient(target_node);
        *response = client->Delete(*request);
        done->Run();
        return;
    }
    
    // 本地处理
    if (!_sm) {
        response->set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("State machine not initialized");
        done->Run();
        return;
    }
    
    ::command::Command command;
    auto cmd = command.mutable_del();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::DeleteReply>(result);

    done->Run();
}

void KVService::Append(::google::protobuf::RpcController* controller,
            const ::command::AppendCommand* request,
            ::command::AppendReply* response,
            ::google::protobuf::Closure* done)
{
    std::string key = request->key();
    
    // 如果启用了分片，检查是否需要转发
    if (_shard_manager && !_shard_manager->IsShardLeader(_shard_manager->GetShard(key), _name)) {
        // 需要转发到主节点
        uint32_t shard_id = _shard_manager->GetShard(key);
        std::string target_node = _shard_manager->GetLeaderForShard(shard_id);
        
        if (target_node.empty()) {
            response->set_err(::command::StateCode::ErrWrongGroup);
            LOG_ERROR("Shard {} has no leader", shard_id);
            done->Run();
            return;
        }
        
        // 转发请求到主节点
        auto client = GetOrCreateClient(target_node);
        *response = client->Append(*request);
        done->Run();
        return;
    }
    
    // 本地处理
    if (!_sm) {
        response->set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("State machine not initialized");
        done->Run();
        return;
    }
    
    ::command::Command command;
    auto cmd = command.mutable_append();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::AppendReply>(result);

    done->Run();
}

void KVService::SetStateMachine(std::shared_ptr<KVStateMathine> sm)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _sm = sm;
}

void KVService::SetShardManager(std::shared_ptr<ShardManager> shard_manager)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _shard_manager = shard_manager;
}

void KVService::UpdateNodes(const std::vector<std::string>& nodes)
{
    if (!_shard_manager) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _shard_manager->UpdateNodes(nodes);
    
    // 清理不再存在的节点的客户端连接
    std::unordered_map<std::string, std::shared_ptr<KVClient>> new_clients;
    for (const auto& node : nodes) {
        auto it = _node_clients.find(node);
        if (it != _node_clients.end()) {
            new_clients[node] = it->second;
        }
    }
    _node_clients = std::move(new_clients);
}

std::shared_ptr<KVClient> KVService::GetOrCreateClient(const std::string& node_name)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    auto it = _node_clients.find(node_name);
    if (it != _node_clients.end()) {
        return it->second;
    }
    
    // 创建新的客户端
    auto [ip, port] = ParseNodeAddress(node_name);
    auto client = std::make_shared<KVClient>(ip, port);
    _node_clients[node_name] = client;
    
    LOG_INFO("Created KVClient for node: {}", node_name);
    return client;
}

std::pair<std::string, uint16_t> KVService::ParseNodeAddress(const std::string& node_name) const
{
    size_t pos = node_name.find(':');
    if (pos == std::string::npos) {
        return {"", 0};
    }
    
    std::string ip = node_name.substr(0, pos);
    uint16_t port = static_cast<uint16_t>(std::stoi(node_name.substr(pos + 1)));
    return {ip, port};
}

// 拉取分片数据（用于迁移）
void KVService::PullShard(::google::protobuf::RpcController* controller,
                          const ::command::PullShardCommand* request,
                          ::command::PullShardReply* response,
                          ::google::protobuf::Closure* done)
{
    uint32_t shard_id = request->shard_id();
    
    LOG_INFO("收到拉取分片{}数据的请求", shard_id);
    
    // 检查是否有分片管理器
    if (!_shard_manager)
    {
        response->set_err(::command::StateCode::ErrWrongGroup);
        done->Run();
        return;
    }
    
    // 检查当前节点是否拥有该分片（作为主节点或副本）
    auto shards = _shard_manager->GetShardsForNode(_name);
    bool has_shard = std::find(shards.begin(), shards.end(), shard_id) != shards.end();
    
    if (!has_shard)
    {
        LOG_WARN("节点{}不拥有分片{}", _name, shard_id);
        response->set_err(::command::StateCode::ErrWrongGroup);
        done->Run();
        return;
    }
    
    // 导出分片数据
    auto get_shard_func = [this](const std::string& key) -> uint32_t {
        return _shard_manager->GetShardForKey(key);
    };
    
    auto shard_data = _db->ExportShardData(shard_id, get_shard_func);
    
    // 填充响应
    response->set_err(::command::StateCode::OK);
    response->set_shard_id(shard_id);
    for (const auto& [key, value] : shard_data)
    {
        (*response->mutable_data())[key] = value;
    }
    
    LOG_INFO("成功导出分片{}的数据，共{}条记录", shard_id, shard_data.size());
    done->Run();
}

// 删除分片数据（迁移完成后清理）
void KVService::DeleteShard(::google::protobuf::RpcController* controller,
                            const ::command::DeleteShardCommand* request,
                            ::command::DeleteShardReply* response,
                            ::google::protobuf::Closure* done)
{
    uint32_t shard_id = request->shard_id();
    
    LOG_INFO("收到删除分片{}数据的请求", shard_id);
    
    // 检查是否有分片管理器
    if (!_shard_manager)
    {
        response->set_err(::command::StateCode::ErrWrongGroup);
        done->Run();
        return;
    }
    
    // 检查当前节点是否拥有该分片
    if (!_shard_manager->IsLocalShard(shard_id, _name))
    {
        LOG_WARN("节点{}不拥有分片{}，无法删除", _name, shard_id);
        response->set_err(::command::StateCode::ErrWrongGroup);
        done->Run();
        return;
    }
    
    // 删除分片数据
    auto get_shard_func = [this](const std::string& key) -> uint32_t {
        return _shard_manager->GetShardForKey(key);
    };
    
    bool success = _db->DeleteShardData(shard_id, get_shard_func);
    
    if (success)
    {
        response->set_err(::command::StateCode::OK);
        LOG_INFO("成功删除分片{}的数据", shard_id);
    }
    else
    {
        response->set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("删除分片{}的数据失败", shard_id);
    }
    
    done->Run();
}
