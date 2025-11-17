#ifndef KVSERVICE_H
#define KVSERVICE_H

#include "common/persister/persister.h"
#include "common/storage/rocksdb_client.h"
#include "kvservice_interface.h"
#include "proto/command.pb.h"
#include "storage/raft/raft_client.h"
#include "storage/raft/state_mathine.h"
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <variant>
#include <vector>

class KVService : public KVServiceInterface, std::enable_shared_from_this<KVService>, public ::command::CommandServiceRPC
{
public:
    using Result = std::variant<::command::GetReply, ::command::PutReply, ::command::DeleteReply, ::command::AppendReply>;

    KVService(std::shared_ptr<RocksDBClient> db,
            std::shared_ptr<Persister> persister,
            std::vector<std::shared_ptr<RaftClient>>& peer_conns,
            std::shared_ptr<KVStateMathine> sm,
            const std::string& name);
    ~KVService();

    // 向 statemachine 暴露的接口
    ::command::GetReply Get(::command::GetCommand command) override;
    ::command::PutReply Put(::command::PutCommand command) override;
    ::command::DeleteReply Delete(::command::DeleteCommand command) override;
    ::command::AppendReply Append(::command::AppendCommand command) override;
    std::vector<uint8_t> snapshot() override;
    void restore(const std::vector<uint8_t>& snapshot) override;

    // rpc 服务接口
    void Get(::google::protobuf::RpcController* controller,
             const ::command::GetCommand* request,
             ::command::GetReply* response,
             ::google::protobuf::Closure* done) override;
             
    void Put(::google::protobuf::RpcController* controller,
             const ::command::PutCommand* request,
             ::command::PutReply* response,
             ::google::protobuf::Closure* done) override;
             
    void Delete(::google::protobuf::RpcController* controller,
                const ::command::DeleteCommand* request,
                ::command::DeleteReply* response,
                ::google::protobuf::Closure* done) override;
    
    void Append(::google::protobuf::RpcController* controller,
                const ::command::AppendCommand* request,
                ::command::AppendReply* response,
                ::google::protobuf::Closure* done) override;

private:
    std::shared_mutex _mutex;

    std::string _name;
    std::shared_ptr<RocksDBClient> _db;
    std::shared_ptr<Persister> _persister;

    std::shared_ptr<KVStateMathine> _sm;
    
    // 客户端状态记录
    std::unordered_map<std::string, uint64_t> _last_request_ids;
    std::unordered_map<std::string, Result> _last_replis;
};

#endif