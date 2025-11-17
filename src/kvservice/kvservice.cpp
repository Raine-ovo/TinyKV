#include "kvservice/kvservice.h"
#include "proto/command.pb.h"
#include "storage/raft/state_mathine.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>
#include "common/boost/serializer.h"

KVService::KVService(std::shared_ptr<RocksDBClient> db,
        std::shared_ptr<Persister> persister,
        std::vector<std::shared_ptr<RaftClient>>& peer_conns,
        std::shared_ptr<KVStateMathine> sm,
        const std::string& name)
    : _db(db), _persister(persister), _name(name), _sm(sm)
{
}


KVService::~KVService()
{
}

// 向 statemachine 暴露的接口
::command::GetReply KVService::Get(::command::GetCommand command)
{
    std::string key = command.key();
    std::string value;

    ::command::GetReply reply = _db->KVGet(key, value);
    return reply;
}

::command::PutReply KVService::Put(::command::PutCommand command)
{
    std::string key = command.key();
    std::string value = command.value();

    ::command::PutReply reply = _db->KVPut(key, value);
    return reply;
}

::command::DeleteReply KVService::Delete(::command::DeleteCommand command)
{
    std::string key = command.key();

    ::command::DeleteReply reply = _db->KVDelete(key);
    return reply;
}

::command::AppendReply KVService::Append(::command::AppendCommand command)
{
    std::string key = command.key();
    std::string value = command.value();

    ::command::AppendReply reply = _db->KVAppend(key, value);
    return reply;
}

std::vector<uint8_t> KVService::snapshot()
{
    std::vector<uint8_t> encode;

    auto all_kvs = _db->GenerateKVSnapshot();
    auto all_kvs_str = Serializer::SerializeToString(all_kvs);
    encode.emplace_back(all_kvs_str.size());
    encode.insert(encode.end(), all_kvs_str.begin(), all_kvs_str.end());

    auto last_request_str = Serializer::SerializeToString(_last_request_ids);
    encode.emplace_back(last_request_str.size());
    encode.insert(encode.end(), last_request_str.begin(), last_request_str.end());

    auto last_replis_str = Serializer::SerializeToString(_last_replis);
    encode.insert(encode.end(), last_replis_str.begin(), last_replis_str.end());

    return encode;
}

void KVService::restore(const std::vector<uint8_t>& snapshot)
{
    // 先解码
    size_t offset = 0;

    size_t all_kvs_len;
    memcpy(&all_kvs_len, snapshot.data(), sizeof(size_t));
    offset += sizeof(size_t);
    std::string all_kvs_str;
    memcpy(all_kvs_str.data(), snapshot.data() + offset, sizeof(all_kvs_len));
    offset += all_kvs_len;
    std::unordered_map<std::string, std::string> all_kvs;
    Serializer::UnserializeFromString(all_kvs, all_kvs_str);
    _db->InstallKVSnapshot(all_kvs);

    _last_replis.clear();
    _last_request_ids.clear();

    size_t last_request_len;
    memcpy(&last_request_len, snapshot.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);
    std::string last_request_str;
    memcpy(last_request_str.data(), snapshot.data() + offset, sizeof(last_request_len));
    offset += sizeof(last_request_len);
    Serializer::UnserializeFromString(_last_request_ids, last_request_str);

    size_t last_replis_len;
    memcpy(&last_replis_len, snapshot.data() + offset, sizeof(size_t));
    offset += sizeof(size_t);
    std::string last_replis_str;
    memcpy(last_replis_str.data(), snapshot.data() + offset, sizeof(last_replis_len));
    offset += sizeof(last_replis_len);
    Serializer::UnserializeFromString(_last_replis, last_replis_str);
}

// rpc 服务接口
void KVService::Get(::google::protobuf::RpcController* controller,
            const ::command::GetCommand* request,
            ::command::GetReply* response,
            ::google::protobuf::Closure* done)
{
    ::command::Command command;
    auto cmd = command.mutable_get();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::GetReply>(result);

    done->Run();
}
            
void KVService::Put(::google::protobuf::RpcController* controller,
            const ::command::PutCommand* request,
            ::command::PutReply* response,
            ::google::protobuf::Closure* done)
{
    ::command::Command command;
    auto cmd = command.mutable_put();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::PutReply>(result);

    done->Run();
}
            
void KVService::Delete(::google::protobuf::RpcController* controller,
            const ::command::DeleteCommand* request,
            ::command::DeleteReply* response,
            ::google::protobuf::Closure* done)
{
    ::command::Command command;
    auto cmd = command.mutable_del();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::DeleteReply>(result);

    done->Run();
}

void KVService::Append(::google::protobuf::RpcController* controller,
            const ::command::AppendCommand* request,
            ::command::AppendReply* response,
            ::google::protobuf::Closure* done)
{
    ::command::Command command;
    auto cmd = command.mutable_append();
    *cmd = *request;
    
    auto [stateCode, result] = _sm->submit(command);

    *response = std::get<::command::AppendReply>(result);

    done->Run();
}
