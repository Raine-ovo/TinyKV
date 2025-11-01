
// 这里的 RpcChannel 主要用于 Raft 节点这种固定连接的场景
// 因此使用长连接更符合要求
// zookeeper 服务发现适用于微服务架构

#ifndef RAFT_RPC_CHANNEL_H
#define RAFT_RPC_CHANNEL_H

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/port.h>
class RaftRpcChannel : public ::google::protobuf::RpcChannel
{
public:
    explicit RaftRpcChannel(const std::string& ip, uint16_t port);
    ~RaftRpcChannel();

    // Rpc Channel 接口
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                google::protobuf::RpcController* controller,
                const google::protobuf::Message* request,
                google::protobuf::Message* response,
                google::protobuf::Closure* done);

    // 连接管理
    bool connectServer();
    bool isConnected() const { return _connected; }
    void disconnected();

private:
    bool sendRequest(const std::string& send_str, ::google::protobuf::RpcController* controller);
    bool receiveResponse(::google::protobuf::Message* response, ::google::protobuf::RpcController* controller);
    std::string serializeRequest(const google::protobuf::MethodDescriptor* method,
                                const google::protobuf::Message* request,
                                google::protobuf::RpcController* controller);
    
private:
    std::string _ip;
    uint16_t _port;

    std::atomic<bool> _connected{false};
    std::atomic<int> _client_fd{-1};
    mutable std::mutex _mutex;
};

#endif