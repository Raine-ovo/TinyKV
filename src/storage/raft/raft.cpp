#include "storage/raft/raft.h"
#include "common/persister/persister.h"
#include "common/timer/timer.h"
#include "proto/command.pb.h"
#include "storage/raft/log_manager.h"
#include "proto/raft.pb.h"
#include "common/logger/logger.h"
#include "proto/persister.pb.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
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

Raft::Raft()
{

}

// 服务层启动 Raft 确认一致性后，由 Leader 向客户端返回结果
// 返回：index，term，isleader
std::tuple<uint32_t, uint64_t, bool> Raft::Start(const ::command::Command& command)
{
    uint32_t index = -1;
    uint64_t term = -1;
    bool isLeader = false;

    std::unique_lock<std::shared_mutex> lock(_mutex);
    term = _currentTerm;
    if (_status != STATUS::LEADER)
    {
        return {index, term, isLeader};
    }

    // 如果是 leader 则进行处理
    Entry one_log{term, command};
    // 追加日志, 由日志复制机制进行一致
    _log_manager.put(one_log);
    
    LOG_INFO("Node[{}] received command and add it to logs.", _me);
    
    index = _log_manager.lastIndex();
    isLeader = true;
    
    return { index, term, isLeader };
}

void Raft::Make(const std::vector<std::shared_ptr<RaftClient>> &peers, const uint32_t &me,
        std::shared_ptr<Persister> persister, std::shared_ptr<ApplyChan<ApplyMsg>> applyChan)
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
    _log_manager.reset();
    _log_manager.set_lastIncludedIndex(0);
    _log_manager.set_lastIncludedTerm(0);

    _matchIndex = std::move(std::vector<uint64_t>(peers.size(), 0));
    _nextIndex = std::move(std::vector<uint64_t>(peers.size(), 1));
    
    // 初始化配置变更相关状态
    _pendingConfigChange = false;
    _configChangeIndex = 0;

    _electTimer = std::make_shared<Timer>();

    _heartbeatTimer = std::make_shared<Timer>();

    // 恢复持久化数据
    ReadPersistData();

    // 启动线程，开始 apply log
    std::thread _thread(std::bind(&Raft::Applier, this));
    _thread.detach();

    // 开启选举超时计时器
    _electTimer->set_random(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT);
    _electTimer->start(Timer::random_time(MIN_ELECT_TIMEOUT, MAX_ELECT_TIMEOUT), true, std::bind(&Raft::ElectPrepare, this), Timer::Mode::Random);

    LOG_INFO("Raft 节点 {} 启动成功", me);
}

Raft::~Raft()
{
    
}

// term、isleader
std::tuple<uint64_t, bool> Raft::GetState()
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
    
    Persist();

    // 因为通过 leader rpc 变为了非 leader ，需要重置计时器
    _electTimer.reset();
}

std::vector<uint8_t> Raft::SerializeLogs()
{
    ::persister::LogEntryVector entries;
    for (const auto& log : _log_manager.get_entries())
    {
        ::raft::LogEntry* one_log = entries.add_entries();
        one_log->set_term(log.term);
        auto cmd = one_log->mutable_command();
        *cmd = log.command;
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

void Raft::UpdatePeers(const std::vector<std::shared_ptr<RaftClient>>& peer_conns)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    size_t new_size = peer_conns.size();
    _peers = peer_conns;

    _nextIndex.resize(new_size, _log_manager.lastIndex() + 1);
    _matchIndex.resize(new_size, 0);

    if (_me >= peer_conns.size())
    {
        Kill();
    }
}

// 提议配置变更（通过日志来安全地变更成员）
std::tuple<uint32_t, uint64_t, bool> Raft::ProposeConfigChange(
    const ::command::ConfigChangeCommand& config_change)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    
    uint32_t index = 0;
    uint64_t term = _currentTerm;
    bool isLeader = (_status == STATUS::LEADER);

    if (!isLeader)
    {
        return {index, term, isLeader};
    }

    // 检查是否有待处理的配置变更
    if (_pendingConfigChange)
    {
        LOG_WARN("Raft[{}] 有待处理的配置变更，拒绝新的配置变更请求", _me);
        return {index, term, false}; // 返回false表示当前不能处理
    }

    // 创建配置变更命令
    ::command::Command command;
    auto* cmd = command.mutable_config_change();
    *cmd = config_change;

    // 追加配置变更日志
    Entry config_log{term, command};
    _log_manager.put(config_log);
    
    index = _log_manager.lastIndex();
    _pendingConfigChange = true;
    _configChangeIndex = index;

    LOG_INFO("Raft[{}] 提议配置变更: type={}, peer_id={}, index={}", 
        _me, 
        config_change.change_type() == ::command::ConfigChangeCommand::ADD_PEER ? "ADD" : "REMOVE",
        config_change.peer_id(),
        index);

    Persist();
    
    return {index, term, isLeader};
}

// 应用配置变更（当配置变更日志被提交后调用）
void Raft::ApplyConfigChange(const ::command::ConfigChangeCommand& config_change)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    LOG_INFO("Raft[{}] 应用配置变更: type={}, peer_id={}", 
        _me,
        config_change.change_type() == ::command::ConfigChangeCommand::ADD_PEER ? "ADD" : "REMOVE",
        config_change.peer_id());

    if (config_change.change_type() == ::command::ConfigChangeCommand::ADD_PEER)
    {
        // 添加节点
        // 注意：这里需要外部提供RaftClient，因为Raft层不应该知道如何创建连接
        // 实际实现中，应该由上层（如KVServer）创建RaftClient后调用UpdatePeers
        // 这里只是标记需要添加节点
        LOG_INFO("Raft[{}] 需要添加节点: peer_id={}, address={}", 
            _me, config_change.peer_id(), config_change.peer_address());
        
        // 如果peer_id超出当前peers大小，需要扩展
        if (config_change.peer_id() >= _peers.size())
        {
            size_t old_size = _peers.size();
            _peers.resize(config_change.peer_id() + 1, nullptr);
            _nextIndex.resize(config_change.peer_id() + 1, _log_manager.lastIndex() + 1);
            _matchIndex.resize(config_change.peer_id() + 1, 0);
            
            LOG_INFO("Raft[{}] 扩展peers数组: {} -> {}", _me, old_size, _peers.size());
        }
    }
    else if (config_change.change_type() == ::command::ConfigChangeCommand::REMOVE_PEER)
    {
        // 移除节点
        uint32_t peer_id = config_change.peer_id();
        
        if (peer_id < _peers.size())
        {
            // 将peer设置为nullptr（标记为移除）
            _peers[peer_id] = nullptr;
            
            // 如果是自己，则退出
            if (peer_id == _me)
            {
                LOG_WARN("Raft[{}] 自己被移除，退出集群", _me);
                Kill();
            }
            else
            {
                LOG_INFO("Raft[{}] 移除节点: peer_id={}", _me, peer_id);
            }
        }
    }

    // 清除待处理标记
    if (_configChangeIndex > 0 && _commitIndex >= _configChangeIndex)
    {
        _pendingConfigChange = false;
        _configChangeIndex = 0;
    }

    Persist();
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

void Raft::Persist()
{
    if (_persister == nullptr)
    {
        LOG_ERROR("raft node {}'s persister is nullptr!", _me);
        return ;
    }

    // 把 logs 进行序列化
    _persister->save_state(_currentTerm, _votedFor, std::move(SerializeLogs()));
}

uint64_t Raft::PersistBytes()
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    
    return _persister->state_size();
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

// 截断 index 及其之前的日志数据
void Raft::Snapshot(uint64_t index, std::vector<uint8_t> snapshot)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    _log_manager.drop(index, true);
    Persist(snapshot);
}

// 定时器触发函数
void Raft::ElectPrepare()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    LOG_INFO("Raft[{}] 开始一次选举", _me);
    
    // leader 不会进行 elect
    if (_status == STATUS::LEADER) return ;

    _status = STATUS::CANDIDATE;

    // 1. 增加任期
    ++ _currentTerm;
    // 2. 给自己投票
    _votedFor = _me;

    Persist();

    ::raft::RequestVoteRequest request;
    request.set_term(_currentTerm);
    request.set_candidateid(_me);
    request.set_lastlogindex(_log_manager.lastIndex());
    request.set_lastlogterm(_log_manager.lastTerm());

    LOG_DEBUG("[{}] start one election, term={}", _me, _currentTerm);

    // 3. 启动线程去执行选举
    std::thread td(std::bind(&Raft::Elect, this, request));
    td.detach();
    
    // 4. 重置计时器
    _electTimer.reset();
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
                    // 更新 nextIndex 和 matchIndex
                    std::for_each(_nextIndex.begin(), _nextIndex.end(), [&](uint64_t& index) { index = _log_manager.lastIndex() + 1; });
                    std::for_each(_matchIndex.begin(), _matchIndex.end(), [](uint64_t& index) { index = 0; });
                    // 立刻开始发送心跳确认身份
                    _heartbeatTimer->start(HEARTBEAT_TIMEOUT, true, std::bind(&Raft::HeartBeat, this), Timer::Mode::Normal);
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
    {
        std::shared_lock<std::shared_mutex> lock(_mutex);
        if (_status != STATUS::LEADER)
        {
            return ;
        }
    }

    // 在回调函数中，开启线程执行 AppendEntries
    std::thread td([this]() {
        this->AppendEntries();
    });
    td.detach();
}

void Raft::UpdateCommitIndex()
{
    // ! 这里一定要用 uint64_t！！！因为 commitIndex=0 的时候，--N虽然会把N变为-1，但是最终比较的时候是会隐式转换为uint
    for (uint64_t N = _log_manager.lastIndex(); N >= _commitIndex; -- N)
    {
        if (N == 0 && _commitIndex == 0)
        {
            // 防止下溢
            break;
        }

        // 只更新自己任期的日志
        LOG_DEBUG("[{}] 在 update commitindex 处 log get={}", _me, N);
        if (_log_manager.get(N).term != _currentTerm)
        {
            continue;
        }

        int matched = 1;
        for (int i = 0; i < _peers.size(); ++ i)
        {
            if (i == _me) continue;
            if (_matchIndex[i] >= N)
            {
                ++ matched;
            }
        }
        if (matched > _peers.size() / 2)
        {
            _commitIndex = N;
            break;
        }
    }
}

// leader 给 peers 发送日志, 在线程中执行
void Raft::AppendEntries()
{

    // 并行执行 RPC
    std::vector<std::thread> waitGroups;
    waitGroups.reserve(_peers.size());

    for (int i = 0; i < _peers.size(); ++ i)
    {
        // 需要手动传递 i 参数，防止循环遍历并发引用
        waitGroups.emplace_back([&](int index) {
                        
            LOG_INFO("AppendEntries: lastIndex={}, _nextIndex[{}]={}", 
                _log_manager.lastIndex(), index, _nextIndex[index]);

            auto peer = _peers[index];
            if (!peer) return ; // me

            // nextIndex 可能在 rpc 途中修改，需要记住原始值
            uint64_t pre_log_index = 0;
            uint64_t log_entries_size = 0;

            ::raft::AppendEntriesRequest request;
            
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);

                // 检查是否落后太多
                if (_nextIndex[index] <= _log_manager.get_lastIncludedIndex())
                {
                    InstallSnapshot(index);
                    return ;
                }

                // ! 公共部分不要写在外面，不然会导致多个线程同时修改同一个对象的资源竞争问题
                request.set_term(_currentTerm);
                request.set_leaderid(_me);
                request.set_leadercommit(_commitIndex);

                pre_log_index = _nextIndex[index] - 1;
                request.set_prevlogindex(pre_log_index);
                LOG_DEBUG("在分发 appendentries 处 log get={}", pre_log_index);
                request.set_prevlogterm(_log_manager.get(pre_log_index).term);
                // 获取 entries
                log_entries_size = _log_manager.lastIndex() - _nextIndex[index] + 1;
                ::google::protobuf::RepeatedPtrField<::raft::LogEntry>* logEntries = request.mutable_entries();
                for (size_t i = _nextIndex[index]; i <= _log_manager.lastIndex(); ++ i)
                {
                    ::raft::LogEntry *one_log = logEntries->Add();
                    LOG_DEBUG("在分发 appendentires 时 log get={}", i);
                    one_log->set_term(_log_manager.get(i).term);
                    ::command::Command* cmd = one_log->mutable_command();
                    LOG_DEBUG("在分发 appendentries 处 log get={}", i);
                    *cmd = _log_manager.get(i).command;
                }
            }

            if (pre_log_index+1 > _log_manager.lastIndex())
            {
                assert(request.entries().size() == 0);
            }

            ::raft::AppendEntriesResponse response = peer->AppendEntries(request);

            LOG_INFO("Node[{}] => Node[{}]: 发送日志 {}-{}", _me, index, pre_log_index+1, _log_manager.lastIndex());
            
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);

                if (response.term() > _currentTerm)
                {
                    TurnFollower(response.term());
                    _electTimer.reset();
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
                    UpdateCommitIndex();
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

void Raft::InstallSnapshot(uint32_t index)
{
    auto peer = _peers[index];

    // 不能发送日志了，需要进行快照安装
    ::raft::InstallSnapshotRequest request;
    request.set_term(_currentTerm);
    request.set_leaderid(_me);
    request.set_lastincludedindex(_log_manager.get_lastIncludedIndex());
    request.set_lastincludedterm(_log_manager.get_lastIncludedTerm());
    auto snapshot = _persister->load_snapshot().value();
    request.set_data(std::move(std::string(snapshot.begin(), snapshot.end())));

    ::raft::InstallSnapshotResponse response = peer->InstallSnapshot(request);
    
    if (peer->getController()->Failed())
    {
        return ;
    }

    if (response.term() > _currentTerm)
    {
        TurnFollower(response.term());
        return ;
    }

    // 过期 rpc
    if (_status != STATUS::LEADER && _currentTerm != request.term())
    {
        return ;
    }

    // 接收到了快照
    _matchIndex[index] = std::max(_matchIndex[index], request.lastincludedindex());
    _nextIndex[index] = _matchIndex[index] + 1;

    UpdateCommitIndex();
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
                LOG_DEBUG("在 applier 处 log get={}", idx);
                msg.Command = _log_manager.get(idx).command;

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
    if (_log_manager.lastIndex() == 0) return true;

    uint64_t my_lastLogIndex = _log_manager.lastIndex();
    uint64_t my_lastLogTerm  = _log_manager.lastTerm();

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
    LOG_INFO("[{}] 从 [{}] 接收到日志复制请求, preLogIndex={}", _me, leaderId, preLogIndex);

    std::unique_lock<std::shared_mutex> lock(_mutex);

    ::raft::AppendEntriesResponse response;
    response.set_term(std::max(_currentTerm, term));

    if (term < _currentTerm)
    {
        response.set_success(false);
        return response;
    }

    // leader 发来的 rpc ，重置计时器
    _electTimer.reset();

    if (term > _currentTerm)
    {
        TurnFollower(term);
    }

    // 一致性检测
    // 没有 prevLogIndex 处日志 或 preLogIndex 处日志 term 冲突
    LOG_DEBUG("在接收 appendentries 处 log get={}", preLogIndex);
    if (_log_manager.lastIndex() < preLogIndex || _log_manager.get(preLogIndex).term != prevLogTerm)
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
        if (_log_index > _log_manager.lastIndex()) break;
        LOG_DEBUG("现在要 get _log_index={}", _log_index);
        if (_log_manager.get(_log_index).term != logs[idx].term)
        {
            _log_manager.resize(_log_index);
            Persist();
            break;
        }
    }

    // idx 为 logs 与 _logs 冲突点
    // 冲突点及后面所有的 logs 复制到 _logs
    auto to_add = logs | std::views::drop(idx);
    for (const auto& log : to_add)
    {
        _log_manager.put(log);
    }
    Persist();

    // 更新 commit_index
    if (leaderCommit > _commitIndex)
    {
        _commitIndex = std::min(leaderCommit, _log_manager.lastIndex());
    }

    LOG_INFO("Node[{}] received logs from Node[{}]", _me, leaderId);

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

::raft::InstallSnapshotResponse Raft::InstallSnapshot(uint64_t term, uint32_t leaderId, uint64_t lastIncludedIndex,
                    uint64_t lastIncludedTerm, std::vector<uint8_t> data)
{
    ::raft::InstallSnapshotResponse response;
    response.set_term(std::max(term, _currentTerm));

    if (term < _currentTerm)
    {
        return response;
    }

    if (term > _currentTerm)
    {
        TurnFollower(term);
    }

    if (lastIncludedIndex < _commitIndex)
    {
        // 如果对方的快照比我 commit 还小，说明这个快照已经过期
        return response;
    }

    if (lastIncludedIndex >= _log_manager.lastIndex())
    {
        // leader 的快照覆盖了我的所有日志，全部接受
        // 把日志全部丢弃，并保存快照数据
        _log_manager.reset();
    }
    else
    {
        // 没有全部覆盖，那就把没覆盖的部分保留

        // 这里要作一个一致性检查
        LOG_DEBUG("在 installSnapshot 函数中要 log manager get={}", lastIncludedIndex);
        if (lastIncludedTerm != _log_manager.get(lastIncludedIndex).term)
        {
            // 根据强 leader 原则，舍弃自己的日志
            _log_manager.reset();
        }
        else
        {
            _log_manager.drop(lastIncludedIndex);
        }
    }

    // 更新 metadata
    _commitIndex = std::max(_commitIndex, lastIncludedIndex);

    _log_manager.set_lastIncludedIndex(lastIncludedIndex);
    _log_manager.set_lastIncludedTerm(lastIncludedTerm);
    Persist(data); // 保存元数据和快照

    // 把快照安装到服务层
    std::thread td([&]() {
        ApplyMsg msg;
        msg.CommandValid = false;
        
        msg.SnapshotValid = true;
        msg.Snapshot = std::move(data);
        msg.SnapshotIndex = lastIncludedIndex;
        msg.SnapshotTerm = lastIncludedTerm;

        _applyChan->push(msg);
    });
    td.detach();

    return response;
}

void Raft::InstallSnapshotRPC(::google::protobuf::RpcController* controller,
                    const ::raft::InstallSnapshotRequest* request,
                    ::raft::InstallSnapshotResponse* response,
                    ::google::protobuf::Closure* done)
{
    uint64_t term = request->term();
    uint32_t leaderId = request->leaderid();
    uint64_t lastIncludedIndex = request->lastincludedindex();
    uint64_t lastIncludedTerm = request->lastincludedterm();
    std::string proto_data = request->data();
    std::vector<uint8_t> data (
        reinterpret_cast<const uint8_t*>(proto_data.data()),
        reinterpret_cast<const uint8_t*>(proto_data.data() + proto_data.size())
    );

    *response = InstallSnapshot(term, 
                                leaderId,
                                lastIncludedIndex, 
                                lastIncludedTerm,
                                std::move(data));

    done->Run();
}