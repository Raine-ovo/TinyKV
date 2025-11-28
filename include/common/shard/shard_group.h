#ifndef SHARD_GROUP_H
#define SHARD_GROUP_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * 分片组
 * 一个分片组包含一个分片ID和该分片的多个副本节点
 * 用于实现高可用：一个分片有多个副本，分布在不同节点上
 */
struct ShardGroup
{
    uint32_t shard_id;  // 分片ID
    std::vector<std::string> nodes;  // 该分片的所有副本节点，格式: ["ip1:port1", "ip2:port2", ...]
    std::string leader;  // 当前主节点（Raft leader），如果为空表示未确定
    
    ShardGroup() : shard_id(0) {}
    ShardGroup(uint32_t id, const std::vector<std::string>& n) 
        : shard_id(id), nodes(n), leader("") {}
};

#endif // SHARD_GROUP_H

