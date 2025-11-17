#ifndef ZK_CLIENT_H
#define ZK_CLIENT_H

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <zookeeper/zookeeper.h>
#include <string>

class ZkClient
{
public:
    using WatcherCallback = std::function<void()>;

    ZkClient(int timeout = 3000);
    ~ZkClient();

    // 建立连接
    void connect();
    void close();

    // 节点操作
    bool GetNodeData(const std::string& path, std::string& data);
    bool SetNodeData(const std::string& path, const std::string& data);
    // 创建临时节点
    bool CreateEphemeralNode(const std::string& path, const std::string& data);
    // 创造永久节点
    bool CreatePersistentNode(const std::string& path, const std::string& data);
    // 查询子节点所有信息
    bool GetChildren(const std::string& path, std::vector<std::string>& children);

    // 注册监听器
    void RegisterChildWatcher(const std::string& path, WatcherCallback callback);
    void RegisterDataWather(const std::string& path, WatcherCallback callback);

private:
    // 监听 zookeeper 的全局事件 
    static void GlobalWatcher(zhandle_t *zh, int type, int state,
                    const char *path, void *watcherCtx);
    // 处理会话相关事件
    void HandleSessionEvent(int state);
    // 处理节点操作事件
    void HandleNodeEvent(int type, const char* path);

private:
    std::shared_mutex _mutex;
    // 存储所有节点监听器
    std::unordered_map<std::string, WatcherCallback> _child_watchers;
    // 存储所有数据监听器
    std::unordered_map<std::string, WatcherCallback> _data_watchers;

    // 地址
    std::string _hosts;
    // 超时事件
    int _timeout;
    bool _connected;
    // zookeeper 的会话端口
    zhandle_t *_zk_client;
};

#endif