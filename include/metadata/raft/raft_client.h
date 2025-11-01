#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include "proto/raft.pb.h"
#include "common/rpc/rpc_controller.h"
#include <google/protobuf/service.h>
#include <memory>

// RPC 客户端接口
class RaftClient : raft::RaftService
{
public:
    explicit RaftClient(::google::protobuf::RpcChannel* channel);

    raft::RequestVoteResponse RequestVote(const raft::RequestVoteRequest& args);

    raft::AppendEntriesResponse AppendEntries(const raft::AppendEntriesRequest& args);

    raft::InstallSnapshotResponse InstallSnapshot(const raft::InstallSnapshotRequest& args);

    std::shared_ptr<RpcController> getController() { return _controller; }

private:
    raft::RaftService::Stub _stub; // 存储对应客户端的 stub
    std::shared_ptr<RpcController> _controller;
};

#endif // RPC_CLIENT_H