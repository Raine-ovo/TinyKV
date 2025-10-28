#include "common/rpc/rpc_controller.h"

RpcController::RpcController()
    : m_failed(false),
      m_errText()
{
}

bool RpcController::Failed() const
{
    return m_failed;
}

std::string RpcController::ErrorText() const
{
    return m_errText;
}

void RpcController::Reset()
{
    m_errText = "";
    m_failed = false;
}

void RpcController::SetFailed(const std::string& reason)
{
    m_failed = true;
    m_errText = reason;
}
