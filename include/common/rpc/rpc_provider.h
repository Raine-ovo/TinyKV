#ifndef RPC_PROVIDER_H
#define RPC_PROVIDER_H

#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <unordered_map>
#include <string>
#include <memory>

// 配置信息类
// 1. 服务名称 - 服务描述类
// 2. 服务的所有方法名称 - 服务描述类
struct ServiceInfo
{
    ::google::protobuf::Service* _service; // 服务描述类
    // 每一个类存储所有方法
    // ! unordered_map 的键不能是引用类型
    std::unordered_map<std::string, const ::google::protobuf::MethodDescriptor*> _methodMap;
};

// RPC提供者类，负责处理RPC请求
// 定义了 rpc 的整个过程，从 rpc 请求 -> protobuf 序列化 -> 网络传输 -> 反序列化 -> rpc 响应
class RpcProvider
{
public:
    RpcProvider(std::string ip,
                std::string port);
    // 启动rpc服务节点
    void run();
    // 注册rpc服务
    void registerService(::google::protobuf::Service* service);

private:
    // 网络模块
    std::unique_ptr<muduo::net::TcpServer> _serverPtr;
    std::unique_ptr<muduo::net::EventLoop> _loop;
    // 回调函数
    void onConnection(const muduo::net::TcpConnectionPtr &conn);
    void onMessage(const muduo::net::TcpConnectionPtr &conn,
                   muduo::net::Buffer *buffer,
                   muduo::Timestamp time);
    void sendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                        ::google::protobuf::Message *response);
    
    // 存储 rpc 所有的服务
    std::unordered_map<std::string, ServiceInfo> _serviceMap;
};

#endif // RPC_PROVIDER_H