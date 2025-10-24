#include "common/rpc/rpc_channel.h"
#include "common/logger/logger.h"
#include "proto/rpc_header.pb.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
    
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

    const ::google::protobuf::ServiceDescriptor *sd = method->service();
    const std::string& service_name = sd->name();
    const std::string& method_name = method->name();
    std::string args_str;
    uint32_t header_size = 0, args_size = 0;
    if (!request->SerializeToString(&args_str))
    {
        LOG_ERROR("{}:{} request serialize failed.", service_name, method_name);
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
    }

    header_size = rpcHeader_str.size();

    // 组织发送的字符串
    std::string send_str;
    // * 这里虽然 header_size 是 4B 数据结构，但是由于转化为了 char* ，编译器安装字符串方式插入，则会一直插入直到遇到 char* 的终结符 \0
    send_str.insert(0, std::string((char*)&header_size), 4);
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
    
}