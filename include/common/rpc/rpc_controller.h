#ifndef RPC_CONTROLLER_H
#define RPC_CONTROLLER_H

#include <google/protobuf/service.h>
#include <string>

class RpcController : public ::google::protobuf::RpcController
{
public:
    RpcController();
    bool Failed() const;
    std::string ErrorText() const;

    void Reset();
    void SetFailed(const std::string& reason);

private:
    bool m_failed; // RPC 方法执行过程中的状态
    std::string m_errText; // RPC 方法执行过程中的错误信息
};

#endif