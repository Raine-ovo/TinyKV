#include "common/shard/shard_manager.h"
#include <algorithm>
#include <cstring>

ShardManager::ShardManager(uint32_t num_shards, uint32_t replica_count)
    : _num_shards(num_shards), _replica_count(replica_count)
{
    if (_num_shards == 0) {
        _num_shards = 1;  // 至少1个分片
    }
    if (_replica_count == 0) {
        _replica_count = 3;  // 默认3个副本（推荐奇数）
    }
}

// 获取key对应的分片ID
uint32_t ShardManager::GetShard(const std::string& key) const
{
    uint64_t hash = HashKey(key);
    return hash % _num_shards;
}

// 获取分片对应的主节点（用于读写操作）
std::string ShardManager::GetLeaderForShard(uint32_t shard_id) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _shard_groups.find(shard_id);
    if (it != _shard_groups.end() && !it->second.leader.empty()) {
        return it->second.leader;
    }
    return "";  // 分片未分配或主节点未确定
}

// 获取分片对应的节点名称（兼容旧接口）
std::string ShardManager::GetNodeForShard(uint32_t shard_id) const
{
    return GetLeaderForShard(shard_id);
}

// 获取分片组信息
ShardGroup ShardManager::GetShardGroup(uint32_t shard_id) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _shard_groups.find(shard_id);
    if (it != _shard_groups.end()) {
        return it->second;
    }
    return ShardGroup();  // 返回空分片组
}

// 更新分片组的主节点
void ShardManager::UpdateShardLeader(uint32_t shard_id, const std::string& leader)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    auto it = _shard_groups.find(shard_id);
    if (it != _shard_groups.end()) {
        // 验证leader是否在分片组的节点列表中
        bool found = false;
        for (const auto& node : it->second.nodes) {
            if (node == leader) {
                found = true;
                break;
            }
        }
        if (found) {
            it->second.leader = leader;
        }
    }
}

// 更新分片到节点的映射
void ShardManager::UpdateShardMapping(const std::unordered_map<uint32_t, std::string>& shard_to_node)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _shard_to_node = shard_to_node;
}

// 更新节点列表，自动重新分配分片组
// nodes: 集群中所有节点的地址列表，格式: ["ip1:port1", "ip2:port2", ...]
// 调用后会重新分配所有分片组（使用轮询策略）
void ShardManager::UpdateNodes(const std::vector<std::string>& nodes)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _nodes = nodes;
    RebalanceShardGroups();
}

// 获取所有分片ID
std::vector<uint32_t> ShardManager::GetAllShards() const
{
    std::vector<uint32_t> shards;
    for (uint32_t i = 0; i < _num_shards; ++i) {
        shards.push_back(i);
    }
    return shards;
}

// 获取节点负责的所有分片（作为主节点或副本）
std::vector<uint32_t> ShardManager::GetShardsForNode(const std::string& node_name) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    std::vector<uint32_t> shards;
    
    // 检查节点是否在分片组的节点列表中
    for (const auto& [shard_id, group] : _shard_groups) {
        for (const auto& node : group.nodes) {
            if (node == node_name) {
                shards.push_back(shard_id);
                break;
            }
        }
    }
    return shards;
}

// 检查key是否属于当前节点（检查当前节点是否是主节点或副本）
bool ShardManager::IsLocalShard(const std::string& key, const std::string& current_node) const
{
    uint32_t shard_id = GetShard(key);
    
    // 检查当前节点是否在分片组中
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _shard_groups.find(shard_id);
    if (it != _shard_groups.end()) {
        for (const auto& node : it->second.nodes) {
            if (node == current_node) {
                return true;
            }
        }
    }
    return false;
}

// 检查当前节点是否是分片的主节点
bool ShardManager::IsShardLeader(uint32_t shard_id, const std::string& current_node) const
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    auto it = _shard_groups.find(shard_id);
    if (it != _shard_groups.end()) {
        return it->second.leader == current_node;
    }
    return false;
}

// 使用一致性哈希计算key的哈希值
uint64_t ShardManager::HashKey(const std::string& key) const
{
    // FNV-1a 哈希算法
    uint64_t hash = 14695981039346656037ULL;
    for (char c : key) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

// 重新分配分片组
// 每个分片组包含多个副本节点，使用轮询策略分配
// 例如：10个分片，3个节点，副本数=3
// 分片0: [节点0, 节点1, 节点2] (主节点: 节点0)
// 分片1: [节点1, 节点2, 节点0] (主节点: 节点1)
// 分片2: [节点2, 节点0, 节点1] (主节点: 节点2)
void ShardManager::RebalanceShardGroups()
{
    _shard_groups.clear();
    
    if (_nodes.empty() || _nodes.size() < _replica_count) {
        return;  // 节点数不足，无法分配
    }

    // 为每个分片创建分片组
    for (uint32_t shard_id = 0; shard_id < _num_shards; ++shard_id) {
        ShardGroup group;
        group.shard_id = shard_id;
        
        // 为分片组分配副本节点（使用轮询策略）
        for (uint32_t replica_idx = 0; replica_idx < _replica_count; ++replica_idx) {
            uint32_t node_index = (shard_id + replica_idx) % _nodes.size();
            group.nodes.push_back(_nodes[node_index]);
        }
        
        // 第一个节点作为初始主节点（实际主节点由Raft选举决定）
        if (!group.nodes.empty()) {
            group.leader = group.nodes[0];
        }
        
        _shard_groups[shard_id] = group;
    }
}

