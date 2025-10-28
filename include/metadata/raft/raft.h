#ifndef NODE_H
#define NODE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <mutex>

#include "common/lockqueue/lockqueue.h"
#include "common/timer/timer.h"
#include "proto/raft.pb.h"
#include "raft_client.h"
#include "common/persister/persister.h"

/*
 * Raft 算法实现：
 * 1. protobuf 构建 rpc 服务
 * 2. 构建 RaftClient 实现 rpc 服务
 * 3. 构建 Raft ，peers中包含 RaftClient 以调用对端服务
 */

const uint HEARTBEAT_TIMEOUT = 100;
const uint MIN_ELECT_TIMEOUT = 400;
const uint MAX_ELECT_TIMEOUT = 800;

enum class STATUS
{
    FOLLOWER,
    CANDIDATE,
    LEADER,
};

// 提交到上层的信息
struct ApplyMsg
{
    bool CommandValid; // 提交的信息是命令
    std::string Command;
    uint64_t CommandIndex; // 日志索引

    bool SnapshotValid; // 提交的信息是快照
    std::string Snapshot;
    uint64_t SnapshotIndex; // 索引
};

struct Entry
{
    uint64_t term;
    std::string command;
};

class Raft : public raft::RaftService
{
public:
    Raft() = delete; // 只使用 Make 作为构建 Raft 的方法
    ~Raft();

    void Make(std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyChan);

    // 服务层启动 Raft 确认一致性后，向客户端返回结果
    void Start();

    void Kill();

private:
    std::shared_mutex _mutex;
    uint32_t _me; // 自己的id
    std::vector<std::shared_ptr<RaftClient>> _peers;
    std::shared_ptr<Persister> _persister;
    std::atomic<bool> _isDead;

    // 持久化状态
    uint64_t _currentTerm;
    uint32_t _votedFor;
    std::vector<Entry> _logs;

    // 自身状态
    STATUS _status;

    // 记录选举超时时间和心跳时间(ms)
    uint32_t _electTimeout;
    uint32_t _heartbeatTimeout;

    // 定时器
    std::shared_ptr<Timer> _electTimer; // 选举
    std::shared_ptr<Timer> _heartbeatTimer; // 心跳

    // 日志复制
    uint64_t _commitIndex; // 最后一个已经 commit 的日志
    uint64_t _lastApplied; // 最后一个已经 apply 到服务层的日志

    // 领导需要维护的状态
    std::vector<uint64_t> _matchIndex; // 和 i 节点匹配的日志索引
    std::vector<uint64_t> _nextIndex; // 下一个要给 i 节点传输的日志索引

    // 提交上层信息的通道
    std::shared_ptr<LockQueue<ApplyMsg>> _applyChan;

    void TurnFollower(uint64_t term);

    // 返回状态：term、isleader
    std::tuple<int, bool> GetState();

    // 把日志提交给上层服务
    void Applier();
    bool Killed();    // 选举
    ::raft::AppendEntriesResponse AppendEntriesRPC(uint64_t term, uint32_t leaderId, uint64_t preLogIndex,
                                            uint64_t prevLogTerm, const std::vector<Entry>& logs, uint64_t leaderCommit);
    // 发起一次选举
    void ElectPrepare();
    void Elect(const ::raft::RequestVoteRequest &request);
    
    // 心跳
    void HeartBeat();
    // 日志复制
    void AppendEntries();

    bool newerLogs(uint64_t lastLogIndex, uint64_t lastLogTerm);
    ::raft::RequestVoteResponse RequestVoteRPC(uint64_t term, uint32_t candidateId,
                                            uint64_t lastLogIndex, uint64_t lastLogTerm);

    // 作为服务端提供 rpc 服务
    void RequestVoteRPC(::google::protobuf::RpcController* controller,
                        const ::raft::RequestVoteRequest* request,
                        ::raft::RequestVoteResponse* response,
                        ::google::protobuf::Closure* done);
    void AppendEntriesRPC(::google::protobuf::RpcController* controller,
                        const ::raft::AppendEntriesRequest* request,
                        ::raft::AppendEntriesResponse* response,
                        ::google::protobuf::Closure* done);
};

#endif