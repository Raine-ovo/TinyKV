#ifndef STATE_MATHINE_H
#define STATE_MATHINE_H

#include "common/lockqueue/lockqueue.h"
#include "common/persister/persister.h"
#include "proto/command.pb.h"
#include "storage/raft/command_interface.h"
#include "raft.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include "common/storage/rocksdb_client.h"
#include "storage/raft/raft_client.h"

// 状态码
enum class StateCode
{
    OK,
    ErrWrongLeader,
};

// Raft 状态机
class KVStateMathine
{
public:
    KVStateMathine();
    ~KVStateMathine();

    void Make(const std::vector<std::shared_ptr<RaftClient>>& peers, uint32_t me, std::shared_ptr<Persister> persister, const std::string& db_path, uint64_t max_raft_logs);

    // 提交一个命令（由上层）
    std::pair<StateCode, std::string> submit(const ::command::Command& command);
    // 读取 raft 发送的 commited 命令
    const ::command::Command readApplyChan();
    // 应用命令
    bool apply(const ::command::Command& command);

    void snapshot();

private:
    void readSnapshot();
    void snapshotHandler();

    // 根据快照来恢复状态机状态
    void restore();

    std::variant<bool, std::string> do_command(::command::Command command);
    std::string do_get(::command::GetCommand command);
    bool do_put(::command::PutCommand command);
    bool do_del(::command::DeleteCommand command);
    bool do_increment(::command::IncrementCommand command);

private:
    std::shared_mutex _mutex;
    uint32_t _me;

    Raft _raft;
    std::shared_ptr<LockQueue<ApplyMsg>> _applyChan;
    // 把数据 apply 到数据库中
    std::shared_ptr<RocksDBClient> _db;

    uint64_t _max_raft_logs; // 最长日志长度，超出则进行快照存储
    uint64_t _last_applied; // 已被状态机应用的日志，用于快照

    // 读取 applyChan 的线程
    std::shared_ptr<std::thread> _readApplyChan;

    // 定时检查日志是否溢出，如果溢出就进行快照保存
    std::shared_ptr<Timer> _snapshotHandler;
};

#endif