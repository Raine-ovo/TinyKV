# TinyKV

## 简介
基于 C++20 的分布式键值存储系统，使用 Raft 算法、 ZooKeeper、RocksDB、Muduo 网络库、Protobuf 完成。支持 Raft 一致性、分片、副本高可用、动态成员变更与分片迁移等功能。

## 基本模块

### Raft 算法实现

完成 Raft 选举、日志复制、快照、持久化等功能来实现系统的一致性和可靠性。

实现了 Raft 的**动态成员变更**。通过配置命令和单步变更策略来自动更新连接，避免多步更新可能引发的脑裂。

动态成员变更流程：
1. KVServer 接收配置变更命令，将其 submit 到状态机，并设置状态机的回调函数；
2. 状态机将命令 submit 到对应 raft 组中，当日志被复制到多数派并提交回状态机后，状态机应用配置变更，更新 peers 并调用回调函数；
3. KVServer 更新 raftclient 连接，并更新 raft 的 peers 列表。

实现了 Raft 的**分片组功能**，通过将 key 与分片组映射实现性能提升、负载均衡和故障隔离。

分片组策略：每个分片由一个复制组处理，每个复制组使用主从复制（即独立 Raft 组）。

实现了 Raft 成员变更后的数据迁移功能，系统通过 ZooKeeper 的 watcher 机制检测节点变更，并触发分片组的重分片，比较新旧分片映射并自动通过 PullShard/DeleteShard RPC 实现新旧分片数据迁移。

### RocksDB Client
实现 RocksDB 存储引擎的 API ，支持保存客户端请求数据、Raft 持久化数据、KV 数据等，支持提取/删除分片数据等操作。

使用列簇 kv_cf、raft_meta_cf、client_request_cf 来保存数据，其中 kv_cf 存储基本KV数据；raft_meta_cf 存储 raft 层持久化元数据；client_request_cf 存储每个用户最后执行命令的 requestid、lastreturn 信息，与用户的请求（GET、PUT等）同步更新，实现操作的去重/幂等；

### ZooKeeper Client
实现 ZooKeeper Client API，功能：
1. 在 /kvserver/\{id\}servers 下存储服务器列表 \[server_0, server_1, ...\]，使用 watcher 机制来检测服务器节点变化。
2. 通过 ZooKeeper 找到配置文件，并读取配置信息。

### RPC 框架
使用 Protocol Buffers 定义消息类型和 RPC 服务，实现高效的数据序列化和反序列化，并使用 muduo 网络库作为网络数据收发的基本框架。

### 日志系统
使用**异步双缓冲队列**实现异步日志系统，前后台双Buffer + 后台批量刷盘，减少日志操作对主线程的影响和锁竞争，提高系统性能。

### 定时器
实现基于**事件驱动**的定时器类，支持随机超时和动态重置，适用于 Raft 选举、心跳。

动态重置：使用 `std::condition_variable + wait_until` 实现，无 CPU 空转且能快速 reset ，不影响 Raft 算法实现。

## 架构

```
Client
  │  (Protobuf RPC)
  ▼
KVService (路由/转发/分片协调)
  │
  ▼
KVStateMachine (Raft 状态机)
  │
  ▼
Raft (选举/复制/快照/成员变更)
  │
  ▼
RocksDB (raft_meta_cf / kv_cf / client_cf)
```