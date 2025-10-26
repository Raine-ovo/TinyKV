#include "discovery/zookeeper/zk_client.h"

#include <iostream>
#include <zookeeper/zookeeper.h>

int main ()
{
    ZkClient zh;
    zh.start(); // 建立连接
    zh.create("/test", "test_data", sizeof("test_data"), 0);
    std::cout << zh.get("/test") << std::endl;
    
    zh.create("/test/hello", "world", sizeof("world"), ZOO_EPHEMERAL);
    std::cout << zh.get("/test/hello") << std::endl;
    return 0;
}