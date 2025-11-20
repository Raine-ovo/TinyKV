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
    if (watcherCtx == nullptr)
    {
        LOG_FATAL("ZKClient GlobalWatcher: watcherCtx is null");
        exit(EXIT_FAILURE) ;
    }

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
        this->_conn_sem.release();
    }
    else if (state == ZOO_EXPIRED_SESSION_STATE)
    {
        // 会话过期事件
        this->_connected = false;
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
    Config& config = Config::getInstance();
    std::string conn_str = config.Load("zookeeper.host") + ":"
                         + config.Load("zookeeper.port");
    LOG_INFO("zookeeper start for conn_str => {}", conn_str);

    /* 创建句柄 */
    _zk_client = zookeeper_init(conn_str.c_str(),
                               &ZkClient::GlobalWatcher,
                               _timeout, nullptr, this, 0);
    if (!_zk_client)
    { 
        // 内核态的句柄都没有创建成功
        LOG_FATAL("zookeeper init failed.");
        exit(EXIT_FAILURE);
    }

    /* 等待连接成功 */
    _conn_sem.acquire();

    /* 连接成功后再拿一次锁，只改状态 */
    {
        std::unique_lock<std::shared_mutex> lock(_mutex);
        _connected = true;
    }

    LOG_INFO("zookeeper connected to {}", conn_str);
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