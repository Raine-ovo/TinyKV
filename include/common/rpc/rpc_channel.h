#ifndef RPC_CHANNEL_H
#define RPC_CHANNEL_H

#include <google/protobuf/service.h>

class RpcChannel : public ::google::protobuf::RpcChannel
{
public:
    void CallMethod(const ::google::protobuf::MethodDescriptor* method,
                    ::google::protobuf::RpcController* controller, 
                    const ::google::protobuf::Message* request,
                    ::google::protobuf::Message* response, 
                    ::google::protobuf::Closure* done);
private:
};

#endif // RPC_CHANNEL_H