#include "common/lockqueue/lockqueue.h"
#include "common/storage/rocksdb_client.h"
#include "common/timer/timer.h"
#include "kvservice/kvservice_interface.h"
#include "proto/command.pb.h"
#include "storage/raft/raft.h"
#include "storage/raft/state_mathine.h"
#include "common/logger/logger.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

KVStateMathine::KVStateMathine()
{
}

KVStateMathine::~KVStateMathine()
{
}

void KVStateMathine::Make(const std::vector<std::shared_ptr<RaftClient>>& peers, uint32_t me, std::shared_ptr<Persister> persister, std::shared_ptr<KVServiceInterface> kv_service, uint64_t max_raft_logs)
{
    _me = me;
    _applyChan = std::make_shared<LockQueue<ApplyMsg>>();
    
    _max_raft_logs = max_raft_logs;
    _last_applied = 0;
    
    _raft->Make(peers, me, persister,  _applyChan);

    _kvservice = kv_service;

    readSnapshot(persister); // 状态机重启后需要读取快照恢复状态

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
            _kvservice->restore(msg.Snapshot);
            _last_applied = std::max(_last_applied, msg.SnapshotIndex);
            // 前面的命令无效了
            for (auto &[index, msg] : _waiter)
            {
                msg.queue.push(std::nullopt);
            }
        }
        else if (msg.CommandValid)
        {
            // 解析命令
            ::command::Command command = msg.Command;
            Result rst = do_command(command);
            _last_applied = std::max(_last_applied, msg.CommandIndex);
            
            SubmitMsg& submsg = _waiter[msg.CommandIndex];
            if (submsg.me == _me)
            {
                submsg.queue.push(rst);
            }
            else
            {
                submsg.queue.push(std::nullopt);
            }
        }
        else
        {
            LOG_ERROR("状态机接收到不合法消息.");
        }
    }
}

void KVStateMathine::UpdatePeers(const std::vector<std::shared_ptr<RaftClient>>& peer_conns)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _raft->UpdatePeers(peer_conns);
}

void KVStateMathine::readSnapshot(std::shared_ptr<Persister> persister)
{
    std::vector<uint8_t> snapshot = persister->load_snapshot().value_or(std::vector<uint8_t>{});
    if (!snapshot.empty())
    {
        _kvservice->restore(snapshot);
    }
}


// 判断日志是否超出上限，超出上限则进行快照
void KVStateMathine::snapshotHandler()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (_last_applied == 0) return ;

    uint64_t log_size = _raft->PersistBytes();
    if (_max_raft_logs > 0 && log_size > _max_raft_logs)
    {
        std::vector<uint8_t> snapshot_data = _kvservice->snapshot();
        _raft->Snapshot(_last_applied, snapshot_data);
    }
}


// 上层服务调用，Submit 调用 raft 的 start 接口来完成日志同步
// 等待执行完毕后，返回状态码和信息
std::pair<StateCode, Result> KVStateMathine::submit(const ::command::Command& command)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    auto [index, startTerm, isLeader] = _raft->Start(command);
    
    if (!isLeader)
    {
        ::command::GetReply rpl;
        return std::make_pair<StateCode, Result>(StateCode::ErrWrongLeader, rpl);
    }

    // 使用一个消息队列用来接收所有从 ApplyChan 中收到的由 raft 发送的结果
    // auto queue = std::make_shared<LockQueue<SubmitMsg>>();
    // queue->push({startTerm, _me});
    // _waiter[index] = queue;

    SubmitMsg msg;
    msg.submitTerm = startTerm;
    msg.me = _me;
    _waiter[index] = msg;

    // raft 完成日志复制->applier 需要时间，这里用定时器
    std::pair<StateCode, Result> result;
    std::unique_ptr<Timer> wait = std::make_unique<Timer>(50, [&, this]() {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        SubmitMsg& msg = _waiter[index];
        assert(msg.queue.size() <= 1);
        if (msg.queue.size() == 1)
        {
            auto rst = msg.queue.back();
            auto [now_term, now_isleader] = _raft->GetState();
            if (rst == std::nullopt || now_term != startTerm || !now_isleader)
            {
                result.first = StateCode::ErrWrongLeader;
                result.second = ::command::GetReply{};
            }
            else
            {
                result.first = StateCode::OK;
                result.second = rst.value();
            }
            wait->kill(); // 定时器结束
            return ;
        }
    });

    std::unique_ptr<Timer> max_wait = std::make_unique<Timer>(5000, [&, this]() {
        wait->kill();
        result.first = StateCode::ErrWrongLeader;
        result.second = ::command::GetReply{};
    });

    // 删除 index 处的 queue
    _waiter.erase(index);

    return result;
}

Result KVStateMathine::do_command(::command::Command command)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    if (command.has_get()) return _kvservice->Get(command.get());
    else if (command.has_put()) return _kvservice->Put(command.put());
    else if (command.has_del()) return _kvservice->Delete(command.del());
    else if (command.has_append()) return _kvservice->Append(command.append());
}