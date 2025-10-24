#ifndef NODE_H
#define NODE_H

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>
#include <mutex>

enum class STATUS
{
    FOLLOWER,
    CANDIDATE,
    LEADER,
};

class Entry
{
    std::string command;
    uint16_t term;
};

class Raft
{
public:
    Raft(uint16_t me, const std::vector<uint16_t> &peers);
    ~Raft();

    // 选举
    void ElectLeader();

    // 日志复制
    void AppendEntries();

    // 广播 
    void BroadCast();

    // 把日志提交给上层服务
    void Applier();

    // 服务层启动 Raft 确认一致性后，向客户端返回结果
    void Start();

    void Kill();
    bool Killed();

private:
    std::shared_mutex _mutex;
    uint16_t _me; // 自己的id
    std::vector<uint16_t> _peers;
    std::atomic_bool _isDead;

    // 持久化状态
    uint16_t _currentTerm;
    uint16_t _votedFor;
    std::vector<Entry> _entries;

    // 自身状态
    STATUS _status;

    // 日志复制
    uint16_t _commitIndex;
    uint16_t _lastApplied;

    // 领导需要维护的状态
    std::vector<uint16_t> _matchIndex;
    std::vector<uint16_t> _nextIndex;
};

#endif