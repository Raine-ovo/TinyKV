#ifndef KVSERVER_INTERFACE_H
#define KVSERVER_INTERFACE_H

#include "proto/command.pb.h"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

class KVServiceInterface
{
public:
    using Result = std::variant<bool, std::string>;
    
    virtual ~KVServiceInterface() = default;
    
    virtual ::command::GetReply Get(::command::GetCommand command) = 0;
    virtual ::command::PutReply Put(::command::PutCommand command) = 0;
    virtual ::command::DeleteReply Delete(::command::DeleteCommand command) = 0;
    virtual ::command::AppendReply Append(::command::AppendCommand command) = 0;
    
    virtual std::vector<uint8_t> snapshot() = 0;
    virtual void restore(const std::vector<uint8_t>& snapshot) = 0;
};

#endif