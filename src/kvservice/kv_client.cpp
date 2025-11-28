#include "kvservice/kv_client.h"
#include "common/logger/logger.h"
#include "common/rpc/rpc_controller.h"
#include <google/protobuf/service.h>

KVClient::KVClient(const std::string& ip, uint16_t port)
    : _ip(ip), _port(port)
{
    _channel = std::make_unique<RaftRpcChannel>(ip, port);
    _stub = std::make_unique<::command::CommandServiceRPC::Stub>(_channel.get());
}

::command::GetReply KVClient::Get(const ::command::GetCommand& request)
{
    ::command::GetReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->Get(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient Get failed: {}", controller->ErrorText());
    }
    
    return response;
}

::command::PutReply KVClient::Put(const ::command::PutCommand& request)
{
    ::command::PutReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->Put(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient Put failed: {}", controller->ErrorText());
    }
    
    return response;
}

::command::DeleteReply KVClient::Delete(const ::command::DeleteCommand& request)
{
    ::command::DeleteReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->Delete(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient Delete failed: {}", controller->ErrorText());
    }
    
    return response;
}

::command::AppendReply KVClient::Append(const ::command::AppendCommand& request)
{
    ::command::AppendReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->Append(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient Append failed: {}", controller->ErrorText());
    }
    
    return response;
}

::command::PullShardReply KVClient::PullShard(const ::command::PullShardCommand& request)
{
    ::command::PullShardReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->PullShard(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient PullShard failed: {}", controller->ErrorText());
    }
    
    return response;
}

::command::DeleteShardReply KVClient::DeleteShard(const ::command::DeleteShardCommand& request)
{
    ::command::DeleteShardReply response;
    auto controller = std::make_shared<RpcController>();
    
    _stub->DeleteShard(controller.get(), &request, &response, nullptr);
    
    if (controller->Failed()) {
        response.set_err(::command::StateCode::ErrDBError);
        LOG_ERROR("KVClient DeleteShard failed: {}", controller->ErrorText());
    }
    
    return response;
}

