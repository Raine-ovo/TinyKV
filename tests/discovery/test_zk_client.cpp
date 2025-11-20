#include "discovery/zookeeper/zk_client.h"

#include <iostream>
#include <zookeeper/zookeeper.h>

int main ()
{
    ZkClient zh;
    zh.connect(); // 建立连接
    zh.CreatePersistentNode("/test", "test_data");
    std::string res;
    if (zh.GetNodeData("/test", res))
    {
        std::cout << res << std::endl;
    }
    
    zh.CreateEphemeralNode("/test/hello", "world");
    if (zh.GetNodeData("/test/hello", res))
    {
        std::cout << res << std::endl;
    }
    return 0;
}