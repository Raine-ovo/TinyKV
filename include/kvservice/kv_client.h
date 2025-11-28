#ifndef KV_CLIENT_H
#define KV_CLIENT_H

#include "proto/command.pb.h"
#include "common/rpc/raft_rpc_channel.h"
#include <google/protobuf/service.h>
#include <memory>
#include <string>

/**
 * KV服务客户端，用于转发请求到其他节点
 */
class KVClient
{
public:
    explicit KVClient(const std::string& ip, uint16_t port);
    ~KVClient() = default;

    // 调用远程Get操作
    ::command::GetReply Get(const ::command::GetCommand& request);

    // 调用远程Put操作
    ::command::PutReply Put(const ::command::PutCommand& request);

    // 调用远程Delete操作
    ::command::DeleteReply Delete(const ::command::DeleteCommand& request);

    // 调用远程Append操作
    ::command::AppendReply Append(const ::command::AppendCommand& request);
    
    // 调用远程PullShard操作（迁移）
    ::command::PullShardReply PullShard(const ::command::PullShardCommand& request);
    
    // 调用远程DeleteShard操作（迁移完成）
    ::command::DeleteShardReply DeleteShard(const ::command::DeleteShardCommand& request);

private:
    std::unique_ptr<::google::protobuf::RpcChannel> _channel;
    std::unique_ptr<::command::CommandServiceRPC::Stub> _stub;
    std::string _ip;
    uint16_t _port;
};

#endif // KV_CLIENT_H

