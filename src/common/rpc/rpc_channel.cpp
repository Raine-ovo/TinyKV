#include "common/rpc/rpc_channel.h"
#include "common/logger/logger.h"
#include "discovery/zookeeper/zk_client.h"
#include "proto/rpc_header.pb.h"
#include "common/config/config.h"

#include <cstdint>
#include <cstdlib>
#include <format>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <utility>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void RpcChannel::CallMethod(const ::google::protobuf::MethodDescriptor* method,
                ::google::protobuf::RpcController* controller, 
                const ::google::protobuf::Message* request,
                ::google::protobuf::Message* response, 
                ::google::protobuf::Closure* done)
{
    // 这个 callmethod 是用户调用的 stub 对象的 rpcchannel 中的callmethod，它在
    // 用户提交 rpc 请求后，将参数等信息序列化为字符串，并通过 socket 编程连接对应
    // 的服务器地址（通过 zookeeper 服务发现），发送数据
    // 服务器在收到信息后调用 onMessage 回调函数，对数据进行反序列化得到参数等信息
    // 并调用 service 的 callmethod（不是这个 rpcchannel 的）来执行对应的业务

    // 将信息打包序列化为字符串
    // 服务器端的规则：4B 的 header size + (service_name + method_name + args_size) + 剩下的 args_str

    LOG_ERROR("NOW call callmethod!!!");
    const ::google::protobuf::ServiceDescriptor *sd = method->service();
    const std::string& service_name = sd->name();
    const std::string& method_name = method->name();
    std::string args_str;
    uint32_t header_size = 0, args_size = 0;
    if (!request->SerializeToString(&args_str))
    {
        LOG_ERROR("{}:{} request serialize failed.", service_name, method_name);
        if (controller) controller->SetFailed(std::format("{}:{} request serialize failed.", service_name, method_name));
        return ;
    }
    args_size = args_str.size();

    // ! 这样是错误的，因为我们计算的 header_size 是 RPC HEADER 的 size
    // header_size = service_name.size() + method_name.size() + args_size;
    
    rpc_header::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    std::string rpcHeader_str;
    if (!rpcHeader.SerializeToString(&rpcHeader_str))
    {
        LOG_ERROR("{}:{} rpc header serialize failed.", service_name, method_name);
        if (controller) controller->SetFailed(std::format("{}:{} rpc header serialize failed.", service_name, method_name));
    }

    // ! 注意，这里 str.size() 的出来的是主机字节序，而我们的数据是需要
    // ! 传递到网络上的，网络的数据需要以大端序（网络字节序）传输
    // ! 因此需要用 htonl 把数据转换为大端序
    header_size = rpcHeader_str.size();
    uint32_t net_header_size = htonl(header_size);

    // 组织发送的字符串
    std::string send_str;
    // * 这里虽然 header_size 是 4B 数据结构，但是由于转化为了 char* ，编译器安装字符串方式插入，则会一直插入直到遇到 char* 的终结符 \0
    send_str.insert(0, (char*)&net_header_size, 4);
    send_str += rpcHeader_str;
    send_str += args_str;

    // 打印调试信息
    std::cout << "==============================" << std::endl;
    std::cout << "header_size: " << header_size << std::endl;
    std::cout << "rpc_header_str: " << rpcHeader_str << std::endl;
    std::cout << "service_name: " << service_name << std::endl;
    std::cout << "method_name: " << method_name << std::endl;
    std::cout << "args_str: " << args_str << std::endl;
    std::cout << "==============================" << std::endl;

    // socket 编程连接服务器，首先使用服务发现找到对应服务器
    ZkClient zk_cli;
    zk_cli.connect();
    // 规定服务发现的规则: /service/method, method 存储对应服务器的 host:port
    const std::string &path = "/" + service_name + "/" + method_name;
    std::string conn_str;

    if (zk_cli.GetNodeData(path, conn_str))
    {
        LOG_ERROR("{}:{} is not exist in zookeeper!", service_name, method_name);
        if (controller) controller->SetFailed(std::format("{}:{} is not exist in zookeeper!", service_name, method_name));
        return ;
    }

    LOG_INFO("find {}:{}", service_name, method_name);

    // 找出对应的 host 和 port
    size_t pos = conn_str.find(':');
    if (pos == std::string::npos)
    {
        LOG_INFO("host:port extracted failed => {}.", conn_str);
        if (controller) controller->SetFailed(std::format("host:port extracted failed => {}.", conn_str));
        return ;
    }
    
    auto [host, port] = 
            std::make_pair(conn_str.substr(0, pos), conn_str.substr(pos + 1));

    LOG_INFO("{}:{} find server {}:{}", service_name, method_name, host, port);

    // 把 host 和 port 组装为 sockaddr 供 socket 使用
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET; // ipv4
    server_addr.sin_port = htons(stoi(port));
    server_addr.sin_addr.s_addr = inet_addr(host.c_str());

    // 内核态创建 socket 文件
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        LOG_ERROR("create socket failed.");
        if (controller) controller->SetFailed("create socket failed.");
        exit(EXIT_FAILURE);
    }

    // 把 socket 描述符连接到服务器
    if (-1 == connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr)))
    {
        LOG_ERROR("socket connect {}:{} failed.", host, port);
        if (controller) controller->SetFailed(std::format("socket connect {}:{} failed.", host, port));
        close(clientfd);
        return ;
    }

    // 连接成功，发送 rpc 请求
    ssize_t send_len;
    if (-1 == (send_len = send(clientfd, send_str.c_str(), send_str.size(), 0)))
    {
        LOG_ERROR("socket send rpc to {}:{} failed.", host, port);
        close(clientfd);
        if (controller) controller->SetFailed(std::format("socket send rpc to {}:{} failed.", host, port));
        return ;
    }

    LOG_INFO("channel send {} bytes for {}:{}.", send_len, service_name, method_name);

    // 接受响应
    char recv_buf[1024];
    int recv_size = 0;
    if (-1 == (recv_size=recv(clientfd, recv_buf, sizeof(recv_buf), 0)))
    {
        LOG_ERROR("socket recv rpc to {}:{} failed.", host, port);
        close(clientfd);
        if (controller) controller->SetFailed(std::format("socket recv rpc to {}:{} failed.", host, port));
        return ;
    }

    // 接收响应成功，数据放在 recv_buf 中，数据长度为 recv_size
    std::string response_str(recv_buf, recv_size);
    if (!response->ParseFromString(response_str))
    {
        LOG_ERROR("response {} parse failed.", response_str);
        close(clientfd);
        if (controller) controller->SetFailed(std::format("response {} parse failed.", response_str));
        return ;
    }

    close(clientfd);
}