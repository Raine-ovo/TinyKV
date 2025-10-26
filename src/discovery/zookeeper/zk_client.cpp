#include "discovery/zookeeper/zk_client.h"
#include "common/config/config.h"
#include "common/logger/logger.h"

#include <cstdlib>
#include <functional>
#include <zookeeper/zookeeper.h>
#include <semaphore.h>


void global_wather(zhandle_t *zh, int type, int state,
                    const char *path, void *watcherCtx) {
    // 触发的是与 zookeeper 会话相关的事件
    if (type == ZOO_SESSION_EVENT)
    {
        if (state == ZOO_CONNECTED_STATE)
        {
            // 连接事件
            sem_t *sem = (sem_t*)zoo_get_context(zh);
            sem_post(sem);
        }
    }
};

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
    Config& config = Config::getInstance();
    std::string host = config.Load("zookeeper.host");
    std::string port = config.Load("zookeeper.port");
    std::string conn_str = host + ":" + port; // zk 的连接要求

    LOG_INFO("zookeeper start for conn_str => {}", conn_str);

    // 内核态创建一个用于连接的句柄并尝试连接
    _zk_client = zookeeper_init(conn_str.c_str(), global_wather, 30000, nullptr, nullptr, 0);
    if (nullptr == _zk_client)
    {
        // 内核态的句柄都没有创建成功
        LOG_FATAL("zookeeper init failed.");
        exit(EXIT_FAILURE);
    }

    // 等待创建的句柄成功连接上 zookeeper 服务器
    sem_t sem;
    sem_init(&sem, 0, 0);
    // 把信号量放到句柄的上下文，以便从回调函数中获取这个信号量
    zoo_set_context(_zk_client, &sem);

    // zookeeper 连接成功后执行回调函数，回调函数会触发信号量，这里等待信号量，即连接成功
    sem_wait(&sem);
    LOG_INFO("zookeeper init success.");
}

const std::string ZkClient::get(const std::string& path)
{
    char buffer[64];
    int bufferlen = sizeof(buffer);
    int flag = zoo_get(_zk_client, path.c_str(), 0, buffer, &bufferlen, nullptr);
    if (ZOK == flag) 
    {
        return buffer; // 返回对应 data
    }
    else
    {
        LOG_ERROR("zookeeper get {} failed, return none.", path);
        return "";
    }
}

// state = 0 表示创建永久性节点，其他表示临时性节点
// 临时性节点一般用 ZOO_EPHEMERA 表示
void ZkClient::create(const std::string& path, const std::string& data, uint datalen, int state)
{
    int flag = zoo_exists(_zk_client, path.c_str(), 0, nullptr);
    if (ZNONODE == flag)
    {
        // 如果这个节点还不存在，就建立
        char path_buffer[128];
        int bufferlen = sizeof(path_buffer);
        flag = zoo_create(_zk_client, path.c_str(), data.c_str(), datalen, 
                &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
        if (ZOK == flag)
        {
            LOG_INFO("znode {}:{} create success.", path, data);
        }
        else
        {
            LOG_FATAL("znode {}:{} create failed, flag: {}.", path, data, flag);
        }
    }
}
