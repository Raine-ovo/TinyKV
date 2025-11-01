#include "common/rpc/rpc_provider.h"
#include "rpc.pb.h"

#include <muduo/net/EventLoop.h>

class provider : public rpc::servicerpc
{
public:
    // 业务逻辑
    const std::string getinfo(const std::string &str)
    {
        return "world";
    }

    // protobuf 提供的 rpc 处理方法
    void getinfo(::google::protobuf::RpcController* controller,
                const ::rpc::req* request,
                ::rpc::res* response,
                ::google::protobuf::Closure* done)
    {
        // 解析 request
        std::string str = request->str();
        // 执行业务逻辑
        const std::string res = getinfo(str);
        // 给 response 赋值
        response->set_str(res);
        // 执行回调函数
        done->Run();
    }

private:
};

int main ()
{
    RpcProvider provider("127.0.0.1", "8000");
    provider.registerService(new class provider);
    provider.run();
    return 0;
}