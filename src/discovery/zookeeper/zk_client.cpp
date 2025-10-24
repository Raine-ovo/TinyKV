#include "discovery/zookeeper/zk_client.h"
#include <zookeeper/zookeeper.h>

ZkClient::ZkClient()
    : _zk_client(nullptr)
{
}

ZkClient::~ZkClient()
{
    if (_zk_client != nullptr)
    {
        // 关闭句柄，释放资源
        zookeeper_close(_zk_client);
    }
}

// 建立连接
void ZkClient::start()
{
    
}

const std::string ZkClient::get(const std::string& path)
{

}

const std::string ZkClient::create(const std::string& path, const std::string& data, uint datalen, int state)
{

}
