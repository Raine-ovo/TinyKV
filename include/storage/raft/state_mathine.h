#ifndef STATE_MATHINE_H
#define STATE_MATHINE_H

#include "common/lockqueue/lockqueue.h"
#include "common/persister/persister.h"
#include "kvservice/kvservice_interface.h"
#include "proto/command.pb.h"
#include "raft.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "storage/statecode/statecode.h"
#include "common/storage/rocksdb_client.h"
#include "storage/raft/raft_client.h"

using Result = std::variant<::command::GetReply, ::command::PutReply, ::command::DeleteReply, ::command::AppendReply>;


struct SubmitMsg
{
    uint64_t submitTerm;
    uint32_t me;
    // 这是个大小为 1 的消息队列，在 readApplyChan 时把 command 运行结果写入 queue 中
    std::queue<std::optional<Result>> queue;
};

// Raft 状态机
class KVStateMathine
{
public:
    KVStateMathine();
    ~KVStateMathine();

    void Make(const std::vector<std::shared_ptr<RaftClient>>& peers, uint32_t me, std::shared_ptr<Persister> persister, std::shared_ptr<KVServiceInterface> kv_service, uint64_t max_raft_logs=1000);

    // 提交一个命令（由上层），返回状态码和信息
    std::pair<StateCode, Result> submit(const ::command::Command& command);

    void UpdatePeers(const std::vector<std::shared_ptr<RaftClient>>& peer_conns);

    // 提议配置变更（安全的成员变更方法）
    // 返回: (StateCode, 是否成功)
    std::pair<StateCode, bool> ProposeConfigChange(const ::command::ConfigChangeCommand& config_change);

private:
    void readSnapshot(std::shared_ptr<Persister> persister);
    void snapshotHandler();

    // 读取 raft 发送的 commited 命令
    const ::command::Command readApplyChan();

    Result do_command(::command::Command command);

private:
    std::shared_mutex _mutex;
    uint32_t _me;

    std::shared_ptr<Raft> _raft;
    std::shared_ptr<ApplyChan<ApplyMsg>> _applyChan;
    // 用于 readApplyChan 和上层服务 Submit 之间的信息交互
    // 这里需要进行一个处理：当调用 start 后，leader 发生变化，导致操作丢失
    // 即我们在 queue 中加入这个处理请求，但是还没 apply 就领导权发生变化，那 readApplyChan 就永远
    // 不会收到这个处理结果，就会无限阻塞在这个请求
    // 处理方式：
    // 1. 防止因为 leader 变化导致操作错误，
    // 比较 raft.Start() 时候的任期和当前任期，不一致则 pop 并返回 wrongleader
    // 这里的实现方式是，给每一个请求都申请一个大小为 1 的消息队列进行通信, 同时保留其 index 用于变相判断 index 索引
    // 2. 防止无限阻塞，设置一个最大阻塞时长
    std::unordered_map<uint64_t, SubmitMsg> _waiter;


    uint64_t _max_raft_logs; // 最长日志长度，超出则进行快照存储
    uint64_t _last_applied; // 已被状态机应用的日志，用于快照

    // 读取 applyChan 的线程
    std::shared_ptr<std::thread> _readApplyChan;

    // 定时检查日志是否溢出，如果溢出就进行快照保存
    std::shared_ptr<Timer> _snapshotHandler;

    // 上层服务
    std::shared_ptr<KVServiceInterface> _kvservice;

    // 配置变更回调函数
    std::function<void(const ::command::ConfigChangeCommand&)> _config_change_callback;
};

#endif