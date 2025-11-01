#include "common/lockqueue/lockqueue.h"
#include "common/rpc/rpc_channel.h"
#include "common/rpc/raft_rpc_channel.h"
#include "metadata/raft/raft.h"
#include "metadata/raft/raft_client.h"
#include "common/rpc/rpc_provider.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <vector>

void Start_A_Raft_Node(const std::string& ip, uint32_t port)
{
    uint32_t node_id = port - 8000;

    auto raft = std::make_shared<Raft>();
    // 连接 raft，提供 service
    std::thread td([&]() {
        RpcProvider provider(ip, std::to_string(port));
        provider.registerService(raft.get());
        provider.run();
    });

    // 创建持久化器
    std::string name = std::format("../data/{}", port);
    auto persister = std::make_shared<Persister>(
        name + "/state", name + "/snapshot"
    );

    // 创建日志提交通道
    auto applyChan = std::make_shared<LockQueue<ApplyMsg>>();

    // 创建 peers ，即创建其他连接的 stub
    std::vector<std::shared_ptr<RaftClient>> peers;
    for (int i = 0; i < 3; i ++ )
    {
        uint32_t target_port = 8000 + i;
        if (target_port == port)
        {
            peers.push_back(nullptr); // me
        }
        else
        {
            peers.emplace_back(
                std::make_shared<RaftClient>(new class RaftRpcChannel(ip, target_port))
            );
        }
    }

    // 检查 peers
    for (int i = 0; i < 3; i ++ )
    {
        if (peers[i] == nullptr)
        {
            std::cout << "peer " << i << " is nulltr" << std::endl;
        }
    }

    // 启动 raft 共识算法
    std::cout << "节点 " << node_id << " 启动 raft 共识算法" << std::endl;
    raft->Make(peers, node_id, persister, applyChan);

    // 监听 applyChan 的数据
    std::thread applyChan_thread([&]() {
        while (true)
        {
            try {
                ApplyMsg msg = applyChan->pop();
                std::cout << "节点 " << node_id << " 接收到消息: "
                    << "CommandValid=" << msg.CommandValid
                    << ", Command=" << msg.Command
                    << ", CommandIndex=" << msg.CommandIndex << std::endl;
            } catch (const std::exception &e) {
                std::cout << "节点 " << node_id << " 处理消息异常" << e.what() << std::endl;
                break;
            }
        }
    });

    // 给 raft 节点加入日志
    std::thread add_logs([&]() {
        int index = 0;
        while (true)
        {
            try {
                std::string command = std::format("command_{}", index);
                raft->Start(command);
            } catch (const std::exception& e) {
                std::cout << "日志复制机制异常: " << e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            ++ index;
        }
    });

    std::cout << "节点 " << node_id << " 启动完成" << std::endl;
    td.join();
    applyChan_thread.join();
    add_logs.join();
}

// 传入 ip 、 node_id
// 我们应用的 ip = 127.0.0.1
// node_id = {1, 2, 3} => port = 8000 + node_id
int main (int argc, char** argv)
{
    if (argc != 3)
    {
        std::cout << __FILE__ << " usage: " << " ./test_raft ip node_id(0-2)" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::string ip = argv[1];
    uint32_t node_id = atoi(argv[2]);

    if (node_id < 0 || node_id > 2)
    {
        std::cout << "node_id select from {0, 1, 2}." << std::endl;
        exit(EXIT_FAILURE);
    }

    uint32_t port = 8000 + node_id;
    
    std::cout << "start raft node on " << ip << ":" << port << std::endl;

    try {
        Start_A_Raft_Node(ip, port);
    } catch (const std::exception &e) {
        std::cout << "failed in function: Start_A_Raft_Node." << std::endl;
    }

    return 0;
}