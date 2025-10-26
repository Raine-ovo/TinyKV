#ifndef ZK_CLIENT_H
#define ZK_CLIENT_H

#include <zookeeper/zookeeper.h>
#include <string>

class ZkClient
{
public:
    ZkClient();
    ~ZkClient();

    // 建立连接
    void start();

    const std::string get(const std::string& path);
    void create(const std::string& path, const std::string& data, uint datalen, int state);

private:
    // zookeeper 的会话端口
    zhandle_t *_zk_client;
};

#endif