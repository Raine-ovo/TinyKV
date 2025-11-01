#include "common/rpc/rpc_provider.h"
#include "common/logger/logger.h"
#include "discovery/zookeeper/zk_client.h"
#include "proto/rpc_header.pb.h"

#include <cstdint>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/stubs/callback.h>
#include <memory>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>

#include <functional>
#include <netinet/in.h>
#include <zookeeper/zookeeper.h>

RpcProvider::RpcProvider(std::string ip,
                std::string port)
{
    muduo::net::InetAddress addr(ip, stoi(port));
    _loop = std::make_unique<muduo::net::EventLoop>();
    _serverPtr = std::make_unique<muduo::net::TcpServer>(_loop.get(), addr, "RpcProvider");
}

// 启动rpc服务节点
void RpcProvider::run()
{
    // 对创建的 tcpserver 绑定回调函数
    _serverPtr->setConnectionCallback(std::bind(&RpcProvider::onConnection, this, std::placeholders::_1));
    _serverPtr->setMessageCallback(std::bind(&RpcProvider::onMessage, this,
                                    std::placeholders::_1,
                                    std::placeholders::_2,
                                    std::placeholders::_3));
    // 设置线程数量
    _serverPtr->setThreadNum(4);
    
    // 同时需要把新的服务告知给 zookeeper
    ZkClient zk_cli;
    zk_cli.start();
    // service 为永久性节点，method为临时性节点
    for (auto &sn: _serviceMap)
    {
        //  /service_name 
        std::string service_name = '/' + sn.first;
        zk_cli.create(service_name, "", 0, 0);
        
        for (auto &mn: sn.second._methodMap)
        {
            std::string method_name = service_name + '/' + mn.first;
            std::string method_data = _serverPtr->ipPort();
            zk_cli.create(method_name, method_data, method_data.size(), ZOO_EPHEMERAL);
            LOG_INFO("register {} on {}", method_name, method_data);
        }
    }

    // 启用网络服务
    _serverPtr->start();
    _loop->loop();
}

void RpcProvider::onConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        conn->shutdown();
    }
}

/*
onMessage 就是服务器接收到了客户端发来的读写请求了
因为是客户端请求读写，因此是发来的客户的请求 rpc 通信数据，需要对其进行 protobuf 序列化

因为这里到了网络层，需要考虑 tcp 的粘包问题，即当前数据不到一个最小包长度，后面粘上了下一个请求包的数据
*/
void RpcProvider::onMessage(const muduo::net::TcpConnectionPtr &conn,
                muduo::net::Buffer *buffer,
                muduo::Timestamp time)
{
    // 序列化
    std::string recv_buf = buffer->retrieveAllAsString();
    // LOG_INFO("recv total bytes: {}, info: {}", recv_buf.size(), recv_buf);

    // 这里 C/S 段规定：前 4 个字节表示可变字符串的长度（请求），获取参数
    // 然后后面是获取 request 参数
    
    // uint32_t 为 4 个字节
    uint32_t net_header_size = 0;
    recv_buf.copy((char*)&net_header_size, 4, 0);
    uint32_t header_size = ntohl(net_header_size);
    // LOG_INFO("net header_size: {}, header_size: {}", net_header_size, header_size);

    // 根据 header size 读取 header
    std::string rpc_header_str = recv_buf.substr(4, header_size);
    rpc_header::RpcHeader rpc_header;
    std::string service_name;
    std::string method_name;
    uint32_t args_size;

    // LOG_INFO("rpc_provider:onMessage received message.");

    if (rpc_header.ParseFromString(rpc_header_str))
    {
        service_name = rpc_header.service_name();
        method_name = rpc_header.method_name();
        args_size = rpc_header.args_size();
    }
    else
    {
        // 序列化失败
        LOG_ERROR("rpc_header {} parsed error!", rpc_header_str);
        return ;
    }

    // 再根据 args_size 来获取剩下的 args
    std::string args_str = recv_buf.substr(4 + header_size, args_size);

    std::cout << "==============================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str: " << rpc_header_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "==============================" << std::endl;

    // 得到参数后，使用 protobuf 来序列化并发送
    // 因为要用 protobuf 来序列化，所以还要把数据转化为 protobuf 能看懂的
    // 比如 service 的 request 和 response

    if (!_serviceMap.contains(service_name))
    {
        LOG_ERROR("service_name {} is not exist!", service_name);
        return ;
    }
    if (!_serviceMap[service_name]._methodMap.contains(method_name))
    {
        LOG_ERROR("service_name:method_name {}:{} is not exist!", service_name, method_name);
        return ;
    }

    ::google::protobuf::Service *service = _serviceMap[service_name]._service;
    const ::google::protobuf::MethodDescriptor *method = _serviceMap[service_name]._methodMap[method_name];

    ::google::protobuf::Message *request = service->GetRequestPrototype(method).New();
    ::google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    if (!request->ParseFromString(args_str))
    {
        LOG_ERROR("request {} parse failed!", args_str);
        return ;
    }

    // done 是一个 callmethod 事件结束后的回调函数，这个回调函数我们应该将其继续使用 protobuf 反序列化并返回 response 给 客户端
    ::google::protobuf::Closure *done = 
        ::google::protobuf::NewCallback<RpcProvider, const muduo::net::TcpConnectionPtr&, ::google::protobuf::Message*>(this, &RpcProvider::sendRpcResponse, conn, response);

    // LOG_INFO("Now calling {}:{}", service_name, method_name);
    service->CallMethod(method, nullptr, request, response, done);
}
    

// 注册rpc服务
void RpcProvider::registerService(::google::protobuf::Service* service)
{
    // 需要提供：服务名称，服务类，方法名称，方法描述类才能注册
    const ::google::protobuf::ServiceDescriptor* sd = service->GetDescriptor();
    const std::string service_name = sd->name();
    LOG_INFO("{} registerService.", service_name);
    ServiceInfo service_info;
    service_info._service = service;
    
    // 记录所有方法
    int method_cnt = sd->method_count();
    for (int i = 0; i < method_cnt; i ++ )
    {
        const ::google::protobuf::MethodDescriptor *method_desc = sd->method(i);
        const std::string &method_name = method_desc->name();
        service_info._methodMap.insert({method_name, method_desc});
        LOG_INFO("method {} has been registered by service {}", method_name, service_name);
    }

    // 把这个完整的服务注册到提供者
    _serviceMap.insert({service_name, service_info});
    LOG_INFO("service {} has been registered!", service_name);
}
    
void RpcProvider::sendRpcResponse(const muduo::net::TcpConnectionPtr &conn,
                    ::google::protobuf::Message *response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str))
    {
        // 序列化成功，把 response 通过 tcp 连接返回给客户端
        conn->send(response_str);
    }
    else
    {
        LOG_ERROR("serialize response_str = {} failed.", response_str);
        conn->shutdown();
    }
    // 在 raft 节点之间的连接应该是长连接
    delete response; // 释放动态创建的 response
}