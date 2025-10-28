#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include "proto/raft.pb.h"
#include <google/protobuf/service.h>

// RPC 客户端接口
class RaftClient : raft::RaftService
{
public:
    explicit RaftClient(::google::protobuf::RpcChannel* channel);

    raft::RequestVoteResponse RequestVote(const raft::RequestVoteRequest& args);

    raft::AppendEntriesResponse AppendEntries(const raft::AppendEntriesRequest& args);

    raft::InstallSnapshotResponse InstallSnapshot(const raft::InstallSnapshotRequest& args);

private:
    raft::RaftService::Stub _stub; // 存储对应客户端的 stub
};

#endif // RPC_CLIENT_H