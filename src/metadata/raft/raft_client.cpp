#include "metadata/raft/raft_client.h"
#include "proto/raft.pb.h"
#include <google/protobuf/service.h>

RaftClient::RaftClient(::google::protobuf::RpcChannel* channel)
    : _stub(channel)
{
}

// raft 客户端 发送投票
raft::RequestVoteResponse RaftClient::RequestVote(const raft::RequestVoteRequest& args)
{
    raft::RequestVoteResponse response;
    _stub.RequestVoteRPC(nullptr, &args, &response, nullptr);
    return response;
}

raft::AppendEntriesResponse RaftClient::AppendEntries(const raft::AppendEntriesRequest& args)
{
    raft::AppendEntriesResponse response;
    _stub.AppendEntriesRPC(nullptr, &args, &response, nullptr);
    return response;
}

raft::InstallSnapshotResponse RaftClient::InstallSnapshot(const raft::InstallSnapshotRequest& args)
{
    raft::InstallSnapshotResponse response;
    _stub.InstallSnapshotRPC(nullptr, &args, &response, nullptr);
    return response;
}
