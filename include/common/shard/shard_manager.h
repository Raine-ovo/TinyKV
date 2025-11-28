#ifndef SHARD_MANAGER_H
#define SHARD_MANAGER_H

#include "common/shard/shard_group.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * 分片管理器
 * 负责将key映射到分片，以及管理分片组到节点的映射关系
*/
class ShardManager
{
public:
    // num_shards: 分片总数
    // replica_count: 每个分片的副本数，默认3（使用奇数便于Raft形成多数派）
    ShardManager(uint32_t num_shards, uint32_t replica_count = 3);
    ~ShardManager() = default;

    // 根据 key 计算分片ID
    uint32_t GetShard(const std::string& key) const;

    // 获取分片对应的主节点（用于读写操作）
    // 返回空字符串表示未找到主节点
    std::string GetLeaderForShard(uint32_t shard_id) const;

    // 获取分片对应的节点名称（ip:port）- 兼容旧接口，返回主节点
    std::string GetNodeForShard(uint32_t shard_id) const;

    // 获取分片组信息
    ShardGroup GetShardGroup(uint32_t shard_id) const;

    // 更新分片组的主节点（由Raft选举产生）
    void UpdateShardLeader(uint32_t shard_id, const std::string& leader);

    // 更新节点列表，自动重新分配分片
    // 节点列表格式: ["ip1:port1", "ip2:port2", ...]
    void UpdateNodes(const std::vector<std::string>& nodes);

    // 获取所有分片ID
    std::vector<uint32_t> GetAllShards() const;

    // 获取节点负责的所有分片（作为主节点或副本）
    std::vector<uint32_t> GetShardsForNode(const std::string& node_name) const;

    // 检查key是否属于当前节点（检查当前节点是否是主节点或副本）
    bool IsLocalShard(const std::string& key, const std::string& current_node) const;

    // 检查当前节点是否是分片的主节点
    bool IsShardLeader(uint32_t shard_id, const std::string& current_node) const;

    // 获取分片数量
    uint32_t GetNumShards() const { return _num_shards; }

    // 获取副本数量
    uint32_t GetReplicaCount() const { return _replica_count; }


private:
    // 使用一致性哈希计算key的哈希值
    uint64_t HashKey(const std::string& key) const;

    // 重新分配分片组
    void RebalanceShardGroups();

private:
    uint32_t _num_shards;  // 分片总数
    uint32_t _replica_count;  // 每个分片的副本数
    std::vector<std::string> _nodes;  // 集群中所有节点的地址列表，格式: ["ip1:port1", "ip2:port2", ...]
    std::unordered_map<uint32_t, ShardGroup> _shard_groups;  // 分片ID到分片组的映射
    mutable std::shared_mutex _mutex;  // 读写锁保护映射关系
};

#endif

