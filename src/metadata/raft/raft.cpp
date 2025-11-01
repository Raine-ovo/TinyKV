#include "metadata/raft/raft.h"
#include "common/persister/persister.h"
#include "common/timer/timer.h"
#include "proto/raft.pb.h"
#include "common/logger/logger.h"
#include "proto/persister.pb.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <tuple>
#include <vector>
#include <functional>
#include <execution>
#include <ranges>
#include <chrono>

Raft::Raft(std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyChan)
    : _peers(peers),
      _me(me),
      _persister(persister),
      _applyChan(applyChan),
      _isDead(false),

      _currentTerm(0),
      _votedFor(-1),
      _status(STATUS::FOLLOWER),
      _commitIndex(0),
      _lastApplied(0),

      // logs 默认初始 index = 1
      _logs(std::move(std::vector<Entry>(1, {0, ""}))),
      _matchIndex(std::move(std::vector<uint64_t>(peers.size(), 0))),
      _nextIndex(std::move(std::vector<uint64_t>(peers.size(), 0))),

      _electTimer(std::make_shared<Timer>(0, std::bind(&Raft::ElectPrepare, this))),

      _heartbeatTimer(std::make_shared<Timer>(HEARTBEAT_TIMEOUT, std::bind(&Raft::HeartBeat, this)))
{
    // 恢复持久化数据
    ReadPersistData();

    // 启动线程，开始 apply log
    std::thread _thread(std::bind(&Raft::Applier, this));
    _thread.detach();

    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);
    _electTimer->run(); // 开启选举超时计时
}

Raft::Raft()
{

}

void Raft::Make(std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyChan)
{
    _peers = peers;
    _me = me;
    _persister = persister;
    _applyChan = applyChan;
    
    _isDead.store(false);

    _currentTerm = 0;
    _votedFor = -1;
    _status = STATUS::FOLLOWER;
    _commitIndex = 0;
    _lastApplied = 0;

    // logs 默认初始 index = 1
    _logs = std::move(std::vector<Entry>(1, {0, ""}));

    _matchIndex = std::move(std::vector<uint64_t>(peers.size(), 0));
    _nextIndex = std::move(std::vector<uint64_t>(peers.size(), 0));

    _electTimer = std::make_shared<Timer>(0, std::bind(&Raft::ElectPrepare, this));
    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);

    _heartbeatTimer = std::make_shared<Timer>(HEARTBEAT_TIMEOUT, std::bind(&Raft::HeartBeat, this));

    // 恢复持久化数据
    ReadPersistData();

    // 启动线程，开始 apply log
    std::thread _thread(std::bind(&Raft::Applier, this));
    _thread.detach();

    _electTimer->run(); // 开启选举超时计时
}

Raft::~Raft()
{
    
}

// term、isleader
std::tuple<int, bool> Raft::GetState()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    return std::make_tuple(_currentTerm, _status == STATUS::LEADER);
}

void Raft::Kill()
{
    _isDead.store(true);
}

bool Raft::Killed()
{
    return _isDead.load();
}

void Raft::TurnFollower(uint64_t term)
{
    // 因为在调用 TurnFollower 时本身就是在锁环境中，所以这里不用加锁
    _status = STATUS::FOLLOWER;
    _currentTerm = term;
    _votedFor = -1; // 即 uint32_t 最大的数

    // 因为通过 leader rpc 变为了非 leader ，需要重置计时器
    _heartbeatTimer->stop();
    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);
}

std::vector<uint8_t> Raft::SerializeLogs()
{
    ::persister::LogEntryVector entries;
    for (const auto& log : _logs)
    {
        ::persister::LogEntry* one_log = entries.add_entries();
        one_log->set_term(log.term);
        one_log->set_command(log.command);
    }
    std::vector<uint8_t> result(entries.ByteSizeLong());
    entries.SerializeToArray(result.data(), result.size());
    return result;
}

void Raft::UnSerializeLogs(const std::vector<uint8_t>& meta_logs)
{
    ::persister::LogEntryVector entries;
    entries.ParseFromArray(meta_logs.data(), meta_logs.size());
}

// 持久化
// 需要触发持久化时，状态发生变化，因此一定是在锁内，这里不用加锁
void Raft::Persist(const std::vector<uint8_t> snapshot)
{
    if (_persister == nullptr)
    {
        LOG_ERROR("raft node {}'s persister is nullptr!", _me);
        return ;
    }

    // 把 logs 进行序列化
    _persister->save_state(_currentTerm, _votedFor, std::move(SerializeLogs()));
    _persister->save_snapshot(snapshot);
}

void Raft::ReadPersistData()
{
    auto state_opt = _persister->load_state();
    if (state_opt.has_value())
    {
        std::tuple<int, int, std::vector<uint8_t>> state = state_opt.value();
        _currentTerm = std::get<0>(state);
        _votedFor = std::get<1>(state);
        UnSerializeLogs(std::get<2>(state));
    }
}

// 定时器触发函数
void Raft::ElectPrepare()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    // leader 不会进行 elect
    if (_status == STATUS::LEADER) return ;

    _status = STATUS::CANDIDATE;

    // 1. 增加任期
    ++ _currentTerm;
    // 2. 给自己投票
    _votedFor = _me;

    ::raft::RequestVoteRequest request;
    request.set_term(_currentTerm);
    request.set_candidateid(_me);
    request.set_lastlogindex(_logs.size() - 1);
    request.set_lastlogterm(_logs.back().term);

    LOG_DEBUG("[{}] start one election, term={}", _me, _currentTerm);

    // 3. 启动线程去执行选举
    std::thread td(std::bind(&Raft::Elect, this, request));
    td.detach();
    
    // 4. 重置计时器
    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);
}

// 开始进行一次选举，在独立线程中完成
void Raft::Elect(const ::raft::RequestVoteRequest &request)
{
    // 需要启动多个并行 rpc，这里的方法是创建多个线程执行
    std::atomic<uint32_t> votes(1);
    std::vector<std::thread> waitGroups;
    waitGroups.reserve(_peers.size());

    for (int i = 0; i < _peers.size(); ++ i)
    {
        waitGroups.emplace_back([&](int index) {
            auto peer = _peers[index];
            if (!peer) return ; // me

            // 执行 rpc 并获取结果
            ::raft::RequestVoteResponse response
                = peer->RequestVote(request);

            if (peer->getController()->Failed())
            {
                return ;
            }
            
            std::unique_lock<std::shared_mutex> lock(_mutex);

            if (response.term() > _currentTerm)
            {
                TurnFollower(response.term());
                return ;
            }

            // 检查状态是不是已经改变了
            if (_status != STATUS::CANDIDATE || _currentTerm != request.term())
            {
                return ; // 这个 rpc 过期了
            }

            if (response.votegranted())
            {
                ++ votes;
                if (votes > _peers.size() / 2)
                {   
                    LOG_INFO("[{}] became a leader with term={}", _me, _currentTerm);

                    // 变成 leader
                    _status = STATUS::LEADER;
                    // 立刻开始发送心跳确认身份
                    _heartbeatTimer->run();
                }
            }
        }, i);
    }

    for (auto& td : waitGroups)
    {
        if (td.joinable())
        {
            td.join();
        }
    }
}

// 心跳计时器回调函数
void Raft::HeartBeat()
{
    // 在回调函数中，开启线程执行 AppendEntries
    std::thread td([this]() {
        this->AppendEntries();
    });
    td.detach();
}

// leader 给 peers 发送日志, 在线程中执行
void Raft::AppendEntries()
{
    ::raft::AppendEntriesRequest request;

    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        
        request.set_term(_currentTerm);
        request.set_leaderid(_me);
        request.set_leadercommit(_commitIndex);
    }

    // 并行执行 RPC
    std::vector<std::thread> waitGroups;
    waitGroups.reserve(_peers.size());

    for (int i = 0; i < _peers.size(); ++ i)
    {
        // 需要手动传递 i 参数，防止循环遍历并发引用
        waitGroups.emplace_back([&](int index) {
            auto peer = _peers[index];
            if (!peer) return ; // me

            // nextIndex 可能在 rpc 途中修改，需要记住原始值
            uint64_t pre_log_index = 0;
            uint64_t log_entries_size = 0;
            
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);

                pre_log_index = _nextIndex[index] - 1;
                request.set_prevlogindex(pre_log_index);
                request.set_prevlogterm(_logs[pre_log_index].term);
                // 获取 entries
                log_entries_size = _logs.size() - 1 - _nextIndex[index] + 1;
                ::google::protobuf::RepeatedPtrField<::raft::LogEntry>* logEntries = request.mutable_entries();
                for (size_t i = _nextIndex[index]; i < _logs.size(); ++ i)
                {
                    ::raft::LogEntry *one_log = logEntries->Add();
                    one_log->set_term(_logs[i].term);
                    one_log->set_command(_logs[i].command);
                }
            }

            ::raft::AppendEntriesResponse response = peer->AppendEntries(request);

            {
                std::unique_lock<std::shared_mutex> lock(_mutex);

                if (response.term() > _currentTerm)
                {
                    TurnFollower(response.term());
                    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);
                    return ;
                }

                // rpc 是否过期
                if (_status != STATUS::LEADER || _currentTerm != request.term())
                {
                    return ;
                }

                // success 判断一致性
                if (response.success())
                {
                    // 因为部分日志成功复制了，现在需要更新 nextIndex 和 matchIndex
                    _matchIndex[index] = pre_log_index + log_entries_size;
                    _nextIndex[index] = _matchIndex[index] + 1;

                    // 再更新全局的 commit_index
                    for (size_t N = _logs.size() - 1; N > _commitIndex; -- N )
                    {
                        // 只更新自己任期的日志
                        if (_logs[N].term == _currentTerm) {
                            size_t commited = 1; // me
                            for (size_t i = 0; i < _peers.size(); ++ i)
                            {
                                if (i == _me) continue;
                                commited += (_matchIndex[i] >= N);
                            }
                            if (commited > _peers.size() / 2)
                            {
                                // 更新 commit_index
                                _commitIndex = N;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    // 日志的一致性验证失败，回退 nextIndex
                    -- _nextIndex[index];
                }
            }
        }, i);
    }

    for (auto &td: waitGroups)
    {
        if (td.joinable())
        {
            td.join();
        }
    }
}

void Raft::Applier()
{
    // 将 committed 日志提交到服务层（通过 applyChan 通道）
    while (!Killed())
    {
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);

            for (uint64_t idx = _lastApplied + 1; idx <= _commitIndex; ++ idx)
            {
                ApplyMsg msg;
                msg.CommandValid = true;
                msg.CommandIndex = idx;
                msg.Command = _logs[idx].command;

                msg.SnapshotValid = false;

                _applyChan->push(msg);
            }

            _lastApplied = _commitIndex;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 50ms
    }
}

/*  =====  RPC 服务 =====  */
// 这里是作为 Raft 服务端，提供 rpc 服务

// 选举限制：候选者必须比自己日志更新
// 新的定义：
// 1. 候选者的最后一个日志的任期比自己的最后一个日志任期更高
// 2. 若最后日志任期一致，候选者日志长度不小于自己的日志长度
// 因为在 RequestVote 已经加锁，所以这里不需要加锁
bool Raft::newerLogs(uint64_t lastLogIndex, uint64_t lastLogTerm)
{
    if (_logs.size() == 1) return true;

    uint64_t my_lastLogIndex = _logs.size() - 1;
    uint64_t my_lastLogTerm  = _logs.back().term;

    return (
        lastLogTerm > my_lastLogTerm
        || (lastLogTerm == my_lastLogTerm && lastLogIndex >= my_lastLogIndex)
    );
}

::raft::RequestVoteResponse Raft::RequestVoteRPC(uint64_t term, uint32_t candidateId,
                                        uint64_t lastLogIndex, uint64_t lastLogTerm)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    ::raft::RequestVoteResponse response;
    response.set_term(std::max(term, _currentTerm));

    if (term < _currentTerm)
    {
        // 对方是旧的任期
        response.set_votegranted(false);
        return response;
    }

    // 对方任期大，无条件转 follower
    if (term > _currentTerm)
    {
        TurnFollower(term);
    }

    // 没投过，或者投了但是网络阻塞没送过去导致对方重发
    // 需要确保领导者满足：拥有所有已提交的日志
    if ((_votedFor == -1 || _votedFor == candidateId) && newerLogs(lastLogIndex, lastLogTerm))
    {
        response.set_votegranted(true);
    }
    else
    {
        response.set_votegranted(false);
    }

    return response;
}

::raft::AppendEntriesResponse Raft::AppendEntriesRPC(uint64_t term, uint32_t leaderId, uint64_t preLogIndex,
                                            uint64_t prevLogTerm, const std::vector<Entry>& logs, uint64_t leaderCommit)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    ::raft::AppendEntriesResponse response;
    response.set_term(std::max(_currentTerm, term));

    if (term < _currentTerm)
    {
        response.set_success(false);
        return response;
    }

    // leader 发来的 rpc ，重置计时器
    _electTimer->random_reset(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);

    if (term > _currentTerm)
    {
        TurnFollower(term);
    }

    // 一致性检测
    // 没有 prevLogIndex 处日志 或 preLogIndex 处日志 term 冲突
    if (_logs.size() <= preLogIndex || _logs[preLogIndex].term != prevLogTerm)
    {
        response.set_success(false);
        return response;
    }
    
    // 一致性检查成功，进行日志复制
    // 但是日志复制过程可能失败（term 任期不同）
    // 当日志发生冲突时，进行日志截断
    response.set_success(true);
    int idx = 0;
    for (; idx < logs.size(); ++ idx)
    {
        int _log_index = preLogIndex + 1 + idx;
        if (_log_index >= _logs.size()) break;
        if (_logs[_log_index].term != logs[idx].term)
        {
            _logs.resize(_log_index);
            break;
        }
    }

    // idx 为 logs 与 _logs 冲突点
    // 冲突点及后面所有的 logs 复制到 _logs
    auto to_add = logs | std::views::drop(idx);
    std::ranges::copy(to_add, std::back_inserter(_logs));

    // 更新 commit_index
    if (leaderCommit > _commitIndex)
    {
        _commitIndex = std::min(leaderCommit, _logs.size() - 1);
    }

    return response;
}

void Raft::RequestVoteRPC(::google::protobuf::RpcController* controller,
                    const ::raft::RequestVoteRequest* request,
                    ::raft::RequestVoteResponse* response,
                    ::google::protobuf::Closure* done)
{
    *response = RequestVoteRPC(request->term(), 
                            request->candidateid(), 
                            request->lastlogindex(), 
                            request->lastlogterm());
    done->Run();
}

void Raft::AppendEntriesRPC(::google::protobuf::RpcController* controller,
                    const ::raft::AppendEntriesRequest* request,
                    ::raft::AppendEntriesResponse* response,
                    ::google::protobuf::Closure* done)
{
    // logs
    int log_size = request->entries_size();
    std::vector<Entry> logs;
    logs.reserve(log_size);
    for (int i = 0; i < log_size; i ++ )
    {
        ::raft::LogEntry log = request->entries(i);
        logs.emplace_back(log.term(), log.command());
    }

    *response = AppendEntriesRPC(request->term(), 
                              request->leaderid(), 
                              request->prevlogindex(), 
                              request->prevlogterm(),
                              logs,
                              request->leadercommit());
    done->Run();
}