#include "common/lockqueue/lockqueue.h"
#include "common/storage/rocksdb_client.h"
#include "common/timer/timer.h"
#include "proto/command.pb.h"
#include "storage/raft/raft.h"
#include "storage/raft/state_mathine.h"
#include "common/logger/logger.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

KVStateMathine::KVStateMathine()
    : _db(nullptr)
{
}

KVStateMathine::~KVStateMathine()
{
}

void KVStateMathine::Make(const std::vector<std::shared_ptr<RaftClient>>& peers, uint32_t me, std::shared_ptr<Persister> persister, const std::string& db_path, uint64_t max_raft_logs)
{
    _me = me;
    _applyChan = std::make_shared<LockQueue<ApplyMsg>>();
    _db = std::make_shared<RocksDBClient>(db_path);
    _max_raft_logs = max_raft_logs;
    _last_applied = 0;
    
    _raft.Make(peers, me, persister,  _applyChan);

    readSnapshot(); // 状态机重启后需要读取快照恢复状态

    _readApplyChan = std::make_shared<std::thread>(std::bind(&KVStateMathine::readApplyChan, this));
    _snapshotHandler = std::make_shared<Timer>(100, std::bind(&KVStateMathine::snapshotHandler, this));

    _snapshotHandler->run();
}

const ::command::Command KVStateMathine::readApplyChan()
{
    while (true)
    {
        // 因为 lockqueue 本身是线程安全的，这里就不需要加锁了
        ApplyMsg msg = _applyChan->pop();
        std::unique_lock<std::shared_mutex> lock(_mutex);

        if (msg.SnapshotValid)
        {
            // 接收到 snapshot 的消息，此时在初始化阶段，需要恢复状态机状态
            restore();
            _last_applied = std::max(_last_applied, msg.SnapshotIndex);
        }
        else if (msg.CommandValid)
        {
            // 解析命令
            ::command::Command command = msg.Command;
            do_command(command);
            _last_applied = std::max(_last_applied, msg.CommandIndex);
        }
        else
        {
            LOG_ERROR("状态机接收到不合法消息.");
        }
    }
}

std::variant<bool, std::string> KVStateMathine::do_command(::command::Command command)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (command.has_get())
    {
        ::command::GetCommand get_command = command.get();
        return do_get(get_command);
    }
    else if (command.has_put())
    {
        ::command::PutCommand put_command = command.put();
        return do_put(put_command);
    }
    else if (command.has_del())
    {
        ::command::DeleteCommand del_command = command.del();
        return do_del(del_command);
    }
    else if (command.has_increment())
    {
        ::command::IncrementCommand inc_command = command.increment();
        return do_increment(inc_command);
    }
    else
    {
        LOG_ERROR("状态机 {} 接收到不合法的消息.", _me);
        return false;
    }
}

std::string KVStateMathine::do_get(::command::GetCommand command)
{
    std::string value;
    _db->Get(command.key(), value);
    return value;
}

bool KVStateMathine::do_put(::command::PutCommand command)
{
    return _db->Put(command.key(), command.value());
}

bool KVStateMathine::do_del(::command::DeleteCommand command)
{
    return _db->Delete(command.key());
}

bool KVStateMathine::do_increment(::command::IncrementCommand command)
{
    return false; // 暂时先不实现
}