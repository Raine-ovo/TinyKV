#include <iostream>
#include "rpc.pb.h"
#include "common/rpc/rpc_channel.h"
using namespace std;

// 调用者使用 stub 版本进行 rpc 调用
int main ()
{
    rpc::servicerpc_Stub stub(new RpcChannel);
    rpc::req req;
    req.set_str("hello");
    rpc::res res;
    stub.getinfo(nullptr, &req, &res, nullptr);
    cout << res.str() << endl;
    return 0;
}