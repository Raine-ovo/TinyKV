#include "common/rpc/raft_rpc_channel.h"
#include "common/logger/logger.h"
#include "proto/rpc_header.pb.h"
#include <cerrno>
#include <cstdint>
#include <format>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h> 
#include <vector>

RaftRpcChannel::RaftRpcChannel(const std::string& ip, uint16_t port)
    : _ip(ip), _port(port)
{
    LOG_INFO("Creating RaftRpcChannel for {}:{}", _ip, _port);
}

RaftRpcChannel::~RaftRpcChannel()
{
    disconnected();
}

void RaftRpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                               google::protobuf::RpcController* controller,
                               const google::protobuf::Message* request,
                               google::protobuf::Message* response,
                               google::protobuf::Closure* done)
{
    const ::google::protobuf::ServiceDescriptor* sd = method->service();
    std::string service_name = sd->name();
    std::string method_name = method->name();
    // LOG_INFO("RaftRpc 调用 {}:{} 的 {}:{}.", _ip, _port, service_name, method_name);
    
    if (!isConnected() && !connectServer())
    {
        LOG_ERROR("RaftRpcChannel for {}:{} is not connected.", _ip, _port);
        if (controller) controller->SetFailed(std::format("RaftRpcChannel for {}:{} is not connected.", _ip, _port));
        return ;
    }

    // 序列化请求
    std::string send_str = std::move(serializeRequest(method, request, controller));
    if (controller && controller->Failed())
    {
        LOG_ERROR("{}", controller->ErrorText());
        return ;
    }

    // 发送请求
    if (!sendRequest(send_str, controller))
    {
        LOG_ERROR("{}", controller->ErrorText());
        return ;
    }

    // 接受响应
    if (!receiveResponse(response, controller))
    {
        LOG_ERROR("{}", controller->ErrorText());
        return ;
    }

    // LOG_DEBUG("RaftRPC {}:{} to {}:{} completed successfully",
            //   method->service()->name(), method->name(), _ip, _port);
}

std::string RaftRpcChannel::serializeRequest(const ::google::protobuf::MethodDescriptor* method,
                                            const ::google::protobuf::Message* request,
                                            ::google::protobuf::RpcController* controller)
{
    const ::google::protobuf::ServiceDescriptor* sd = method->service();
    const std::string& service_name = sd->name();
    const std::string& method_name = method->name();

    // 序列化请求参数
    std::string args_str;
    if (!request->SerializeToString(&args_str))
    {
        std::string errMsg = std::format("{}:{} request serialize failed.", service_name, method_name);
        LOG_ERROR("RaftRpc {}", errMsg);
        if (controller) controller->SetFailed(errMsg);
        return "";
    }

    uint32_t args_size = args_str.size();

    // 序列化 RPC 头部
    rpc_header::RpcHeader rpc_header;
    rpc_header.set_service_name(service_name);
    rpc_header.set_method_name(method_name);
    rpc_header.set_args_size(args_size);

    std::string header_str;
    if (!rpc_header.SerializeToString(&header_str))
    {
        std::string error_msg = std::format("{}:{} header serialize failed", 
                                           service_name, method_name);
        LOG_ERROR("RaftRPC {}", error_msg);
        if (controller) {
            controller->SetFailed(error_msg);
        }
        return "";
    }

    uint32_t header_size = header_str.size();
    uint32_t net_header_size = htonl(header_size);

    // 组装发送到网络
    std::string send_str;
    send_str.insert(0, reinterpret_cast<const char*>(&net_header_size), 4);
    send_str += header_str;
    send_str += args_str;

    // 打印调试信息
    std::cout << "====================================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str:" << header_str << std::endl;
    std::cout << "service_name:" << service_name << std::endl;
    std::cout << "method_name:" << method_name << std::endl;
    std::cout << "args_str:" << args_str << std::endl;
    std::cout << "====================================" << std::endl;

    // LOG_INFO("RaftRpc {}:{} to {}:{} - header_size: {}, args_size: {}",
        // service_name, method_name, _ip, _port, header_size, args_size);
    return send_str;
}

// 发送 request
bool RaftRpcChannel::sendRequest(const std::string& send_str, 
                                google::protobuf::RpcController* controller)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!isConnected() && !connectServer())
    {
        std::string error_msg = "Not connected to peer";
        LOG_ERROR("RaftRPC send failed: {}", error_msg);
        if (controller) controller->SetFailed(error_msg);
        return false;
    }

    ssize_t send_bytes = send(_client_fd, send_str.c_str(), send_str.size(), 0);
    if (send_bytes == -1)
    {
        std::string errno_msg = std::format("send failed: {}", errno);
        LOG_ERROR("RaftRpc send to {}:{} failed: {}", _ip, _port, errno_msg);
        if (controller) controller->SetFailed(errno_msg);
        // 发送失败，认为连接不可用
        _connected.store(false);
        connectServer(); // 重新连接
        return false;
    }

    if (send_bytes != send_str.size())
    {
        LOG_ERROR("RaftRpc send to {}:{}: {}/{} bytes.", _ip, _port, send_bytes, send_str.size());
    }

    // LOG_INFO("Sent {} bytes to {}:{}", send_bytes, _ip, _port);
    return true;
}

bool RaftRpcChannel::receiveResponse(google::protobuf::Message* response,
                                    google::protobuf::RpcController* controller)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (!isConnected() && connectServer())
    {
        std::string error_msg = "Not connected to peer";
        LOG_ERROR("RaftRPC receive failed: {}", error_msg);
        if (controller) {
            controller->SetFailed(error_msg);
        }
        return false;
    }

    // 这里接收响应数据
    char buffer[4096];
    ssize_t received_bytes = recv(_client_fd, buffer, sizeof(buffer), 0);

    if (-1 == received_bytes)
    {
        std::string error_msg;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            error_msg = "receive timeout";
        }
        else
        {
            error_msg = std::format("receive failed: {}", errno);
        }

        LOG_ERROR("RaftRpc receive from {}:{} failed: {}", _ip, _port, error_msg);

        // 认为连接不可用
        _connected = false;
        _client_fd = -1;
        connectServer();

        if (controller) controller->SetFailed(error_msg);
        return false;
    }

    // 反序列化 response
    std::string response_str(buffer, received_bytes);
    if (!response->ParseFromString(response_str))
    {
        std::string error_msg = "failed to parse response";
        LOG_ERROR("RaftRpc parse response from {}:{} failed.", _ip, _port);
        if (controller) controller->SetFailed(error_msg);
        return false;
    }

    // LOG_INFO("RaftRpc received {} bytes from {}:{}", received_bytes, _ip, _port);
    return true;
}

bool RaftRpcChannel::connectServer()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_connected) return true;
    
    // 关闭现有连接
    if (_client_fd != -1)
    {
        close(_client_fd.load());
        _client_fd.store(-1);
    }

    // 创建 socket
    _client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (_client_fd == -1)
    {
        LOG_ERROR("failed to create socket for {}:{}. errno={}", _ip, _port, errno);
        return false;
    }
    
    // 连接服务器
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(_port);
    server_addr.sin_addr.s_addr = inet_addr(_ip.c_str());

    int ret = connect(_client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == 0)
    {
        _connected = true;
        LOG_INFO("connected to {}:{} successfully", _ip, _port);
        return true;
    }
    else
    {
        LOG_ERROR("connect to {}:{} failed. errno={} ({})", _ip, _port, errno, strerror(errno));
        close(_client_fd);
        _client_fd = -1;
        return false;
    }
}

void RaftRpcChannel::disconnected()
{
    std::lock_guard<std::mutex> lock(_mutex);

    if (_client_fd != -1)
    {
        close(_client_fd);
        _connected.store(false);
        _client_fd = -1;
    }

    LOG_DEBUG("{}:{} Rpc channel 断开连接", _ip, _port);
}