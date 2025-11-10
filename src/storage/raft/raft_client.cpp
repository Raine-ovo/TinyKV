#include "storage/raft/raft_client.h"
#include "common/rpc/rpc_controller.h"
#include "proto/raft.pb.h"
#include <google/protobuf/service.h>
#include <memory>

RaftClient::RaftClient(::google::protobuf::RpcChannel* channel)
    : _stub(channel),
      _controller(std::make_shared<RpcController>())
{
}

// raft 客户端 发送投票
raft::RequestVoteResponse RaftClient::RequestVote(const raft::RequestVoteRequest& args)
{
    raft::RequestVoteResponse response;
    _stub.RequestVoteRPC(_controller.get(), &args, &response, nullptr);
    return response;
}

raft::AppendEntriesResponse RaftClient::AppendEntries(const raft::AppendEntriesRequest& args)
{
    raft::AppendEntriesResponse response;
    _stub.AppendEntriesRPC(_controller.get(), &args, &response, nullptr);
    return response;
}

raft::InstallSnapshotResponse RaftClient::InstallSnapshot(const raft::InstallSnapshotRequest& args)
{
    raft::InstallSnapshotResponse response;
    _stub.InstallSnapshotRPC(_controller.get(), &args, &response, nullptr);
    return response;
}
