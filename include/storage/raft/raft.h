#ifndef NODE_H
#define NODE_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <sys/types.h>
#include <vector>
#include <mutex>

#include "common/lockqueue/lockqueue.h"
#include "common/timer/timer.h"
#include "proto/command.pb.h"
#include "proto/raft.pb.h"
#include "raft_client.h"
#include "common/persister/persister.h"
#include "log_manager.h"
#include "storage/raft/applyChan.h"

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
    ::command::Command Command;
    uint64_t CommandIndex; // 日志索引

    bool SnapshotValid; // 提交的信息是快照
    std::vector<uint8_t> Snapshot;
    uint64_t SnapshotIndex; // 索引
    uint64_t SnapshotTerm;
};

class Raft : public raft::RaftService
{
public:
    Raft(std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<ApplyChan<ApplyMsg>> applyChan);
    Raft();
    ~Raft();

    
    // 返回状态：term、isleader
    std::tuple<uint64_t, bool> GetState();

    void Make(const std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<ApplyChan<ApplyMsg>> applyChan);

    // 读取 state 字节数量
    uint64_t PersistBytes();

    // 服务层启动 Raft 确认一致性后，由 Leader 向客户端返回结果
    // 返回：index，term，isleader
    std::tuple<uint32_t, uint64_t, bool> Start(const ::command::Command& command);

    // 上层使用字节流保存状态机状态，这个状态为快照
    // 然后把这个字节流传给 raft 节点，调用 Snapshot 截断部分日志，并对这个状态机状态进行持久化
    void Snapshot(uint64_t index, std::vector<uint8_t> snapshot);

    void Kill();
    bool Killed();

    void UpdatePeers(const std::vector<std::shared_ptr<RaftClient>>& peer_conns);

private:
    std::shared_mutex _mutex;
    uint32_t _me; // 自己的id
    std::vector<std::shared_ptr<RaftClient>> _peers;
    std::shared_ptr<Persister> _persister;
    std::atomic<bool> _isDead;

    // 持久化状态
    uint64_t _currentTerm;
    uint32_t _votedFor;
    LogManager _log_manager;

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
    std::shared_ptr<ApplyChan<ApplyMsg>> _applyChan;

private:

    void TurnFollower(uint64_t term);


    // 把日志提交给上层服务
    void Applier();
    ::raft::AppendEntriesResponse AppendEntriesRPC(uint64_t term, uint32_t leaderId, uint64_t preLogIndex,
                                            uint64_t prevLogTerm, const std::vector<Entry>& logs, uint64_t leaderCommit);
    // 发起一次选举
    void ElectPrepare();
    void Elect(const ::raft::RequestVoteRequest &request);
    
    // 心跳
    void HeartBeat();
    // 日志复制
    void AppendEntries();
    // Follower 接收日志/快照后更新 matchIndex
    void UpdateCommitIndex();


    // Leader 向落后的 follower 提供快照
    // 在 AppendEntries 中检测是否落后
    void InstallSnapshot(uint32_t index);

    /* 持久化相关函数 */
    // 序列化日志为字节流
    std::vector<uint8_t> SerializeLogs();
    // 反序列化并应用到日志
    void UnSerializeLogs(const std::vector<uint8_t>& meta_logs);
    // 持久化
    void Persist(const std::vector<uint8_t> snapshot);
    void Persist();
    // 创建 Raft 时恢复持久化数据
    void ReadPersistData();

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
    
    ::raft::InstallSnapshotResponse InstallSnapshot(uint64_t term, uint32_t leaderId, uint64_t lastIncludedIndex,
                        uint64_t lastIncludedTerm, std::vector<uint8_t> data);
    void InstallSnapshotRPC(::google::protobuf::RpcController* controller,
                       const ::raft::InstallSnapshotRequest* request,
                       ::raft::InstallSnapshotResponse* response,
                       ::google::protobuf::Closure* done);
};

#endif