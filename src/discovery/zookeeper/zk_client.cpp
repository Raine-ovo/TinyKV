#include "discovery/zookeeper/zk_client.h"
#include "common/config/config.h"
#include "common/logger/logger.h"

#include <cstdlib>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <zookeeper/zookeeper.h>
#include <semaphore.h>
#include <zookeeper/zookeeper.jute.h>


void ZkClient::GlobalWatcher(zhandle_t *zh, int type, int state,
                    const char *path, void *watcherCtx) {
    ZkClient* client = static_cast<ZkClient*>(watcherCtx);
    
    // 触发的是与 zookeeper 会话相关的事件
    if (type == ZOO_SESSION_EVENT)
    {
        client->HandleSessionEvent(state);
    }
    else
    {
        // 节点操作相关事件
        client->HandleNodeEvent(type, path);
    }
};

void ZkClient::HandleSessionEvent(int state)
{
    if (state == ZOO_CONNECTED_STATE)
    {
        // 连接事件
        _connected = true;
        sem_t *sem = (sem_t*)zoo_get_context(_zk_client);
        sem_post(sem);
    }
    else if (state == ZOO_EXPIRED_SESSION_STATE)
    {
        // 会话过期事件
        _connected = false;
    }
}

void ZkClient::HandleNodeEvent(int type, const char* path)
{
    std::string path_str = path ? path : "";

    if (type == ZOO_CHILD_EVENT)
    {
        auto it = _child_watchers.find(path_str);
        if (it != _child_watchers.end())
        {
            // 如果存在对应的监听器，就执行回调函数
            it->second(); 
        }
    }
    else if (type == ZOO_CHANGED_EVENT)
    {
        auto it = _data_watchers.find(path_str);
        if (it != _data_watchers.end())
        {
            it->second();
        }
    }
}

ZkClient::ZkClient(int timeout)
    : _zk_client(nullptr), _timeout(timeout), _connected(false)
{
}

ZkClient::~ZkClient()
{
    close();
}

// 建立连接
void ZkClient::connect()
{
    std::unique_lock<std::shared_mutex> lock(_mutex);

    Config& config = Config::getInstance();
    std::string host = config.Load("zookeeper.host");
    std::string port = config.Load("zookeeper.port");
    std::string conn_str = host + ":" + port; // zk 的连接要求

    LOG_INFO("zookeeper start for conn_str => {}", conn_str);

    // 内核态创建一个用于连接的句柄并尝试连接
    _zk_client = zookeeper_init(conn_str.c_str(), &ZkClient::GlobalWatcher, 30000, nullptr, nullptr, 0);
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

bool ZkClient::CreateEphemeralNode(const std::string& path, const std::string& data)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (!_connected) return false;

    if (zoo_exists(_zk_client, path.c_str(), 0, nullptr) == ZNONODE)
    {
        int result = zoo_create(_zk_client, path.c_str(), data.c_str(), data.length(),
                            &ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL, nullptr, 0);
        if (result != ZOK)
        {
            LOG_ERROR("创建临时 Znode 节点 {} 失败: {}", path, zerror(result));
            return false;
        }
        LOG_INFO("创建临时 Znode 节点 {} 成功.", path);
        return true;
    }
    LOG_ERROR("创建临时 Znode 节点 {} 失败: 已经存在该节点", path);
    return false;
}

bool ZkClient::CreatePersistentNode(const std::string& path, const std::string& data)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    if (!_connected) return false;

    if (zoo_exists(_zk_client, path.c_str(), 0, nullptr) == ZNONODE)
    {
        int result = zoo_create(_zk_client, path.c_str(), data.c_str(), data.length(),
                            &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
        if (result != ZOK)
        {
            LOG_ERROR("创建永久 Znode 节点 {} 失败: {}", path, zerror(result));
            return false;
        }
        LOG_INFO("创建永久 Znode 节点 {} 成功.", path);
        return true;
    }
    LOG_ERROR("创建永久 Znode 节点 {} 失败: 已经存在该节点", path);
    return false;
}

bool ZkClient::GetChildren(const std::string& path, std::vector<std::string>& children)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (!_connected) return false;

    // Zk 的自定义类型，用作 get_children 的参数
    String_vector strings;
    int result = zoo_get_children(_zk_client, path.c_str(), 0, &strings);

    if (result != ZOK)
    {
        LOG_ERROR("无法获取 {} 子节点信息: {}", path.c_str(), zerror(result));
        return false;
    }

    children.clear();
    for (int i = 0; i < strings.count; i ++ )
    {
        children.push_back(strings.data[i]);
    }

    // 因为是动态分配的，需要释放内存
    deallocate_String_vector(&strings);
    return true;
}

bool ZkClient::GetNodeData(const std::string& path, std::string& data)
{
    std::shared_lock<std::shared_mutex> lock(_mutex);
    if (!_connected) return false;

    char buffer[1024];
    int buffer_len = sizeof(buffer);

    int result = zoo_get(_zk_client, path.c_str(), 0, buffer, &buffer_len, nullptr);
    
    if (result != ZOK)
    {
        LOG_ERROR("获取节点 {} 信息失败: {}", path, zerror(result));
        return false;
    }

    data.assign(buffer, buffer_len);
    return true;
}

void ZkClient::RegisterChildWatcher(const std::string& path, WatcherCallback callback)
{
    std::unique_lock<std::shared_mutex> lock(_mutex);
    _child_watchers[path] = callback;

    String_vector children;
    if (zoo_wget_children(_zk_client, path.c_str(), &ZkClient::GlobalWatcher, this, &children) != ZOK)
    {
        LOG_INFO("给节点 {} 注册监听器成功.", path);
    }
}

void ZkClient::close()
{
    if (_zk_client != nullptr)
    {
        // 关闭句柄，释放资源
        zookeeper_close(_zk_client);
    }
}