#include "storage/raft/command_interface.h"
#include "common/logger/logger.h"
#include "proto/command.pb.h"
#include <memory>

PutCommand::PutCommand(const std::string& key, const std::string& value)
    : _key(key), _value(value)
{
}

GetCommand::GetCommand(const std::string& key)
    : _key(key)
{
}

DeleteCommand::DeleteCommand(const std::string& key)
    : _key(key)
{
}

IncrementCommand::IncrementCommand(const std::string& key, int delta)
    : _key(key), _delta(delta)
{
}

::command::Command PutCommand::to_protobuf() const
{
    ::command::Command command;
    auto put_cmd = command.mutable_put();
    put_cmd->set_key(_key);
    put_cmd->set_value(_value);
    return command;
}

::command::Command GetCommand::to_protobuf() const
{
    ::command::Command command;
    auto get_cmd = command.mutable_get();
    get_cmd->set_key(_key);
    return command;
}

::command::Command DeleteCommand::to_protobuf() const
{
    ::command::Command command;
    auto delete_cmd = command.mutable_del();
    delete_cmd->set_key(_key);
    return command;
}

::command::Command IncrementCommand::to_protobuf() const
{
    ::command::Command command;
    auto incre_cmd = command.mutable_increment();
    incre_cmd->set_key(_key);
    incre_cmd->set_delta(_delta);
    return command;
}

void PutCommand::from_protobuf(const ::command::Command& proto_cmd)
{
    _key = proto_cmd.put().key();
    _value = proto_cmd.put().value();
}

void GetCommand::from_protobuf(const ::command::Command& proto_cmd)
{
    _key = proto_cmd.get().key();
}

void DeleteCommand::from_protobuf(const ::command::Command& proto_cmd)
{
    _key = proto_cmd.del().key();
}

void IncrementCommand::from_protobuf(const ::command::Command& proto_cmd)
{
    _key = proto_cmd.increment().key();
    _delta = proto_cmd.increment().delta();
}

std::unique_ptr<Command> PutCommand::clone() const
{
    return std::make_unique<PutCommand>(*this);
}

std::unique_ptr<Command> GetCommand::clone() const
{
    return std::make_unique<GetCommand>(*this);
}

std::unique_ptr<Command> DeleteCommand::clone() const
{
    return std::make_unique<DeleteCommand>(*this);
}

std::unique_ptr<Command> IncrementCommand::clone() const
{
    return std::make_unique<IncrementCommand>(*this);
}

std::string PutCommand::type_name() const
{
    return "Put";
}

std::string GetCommand::type_name() const
{
    return "Get";
}

std::string DeleteCommand::type_name() const
{
    return "Delete";
}

std::string IncrementCommand::type_name() const
{
    return "Increment";
}

std::unique_ptr<Command> CommandFactory::create_from_protobuf(const ::command::Command& proto_cmd)
{
    std::unique_ptr<Command> cmd;

    if (proto_cmd.has_put()) cmd = std::make_unique<PutCommand>();
    else if (proto_cmd.has_get()) cmd = std::make_unique<GetCommand>();
    else if (proto_cmd.has_del()) cmd = std::make_unique<DeleteCommand>();
    else if (proto_cmd.has_increment()) cmd = std::make_unique<IncrementCommand>();
    else LOG_ERROR("proto_cmd unserialized failed.");
    
    cmd->from_protobuf(proto_cmd);
    return cmd;
}

std::unique_ptr<Command> CommandFactory::create_put(const std::string& key, const std::string& value)
{
    return std::make_unique<PutCommand>(key, value);
}

std::unique_ptr<Command> CommandFactory::create_get(const std::string& key)
{
    return std::make_unique<GetCommand>(key);
}

std::unique_ptr<Command> CommandFactory::create_delete(const std::string& key)
{
    return std::make_unique<DeleteCommand>(key);
}

std::unique_ptr<Command> CommandFactory::create_increament(const std::string& key, int delta)
{
    return std::make_unique<IncrementCommand>(key, delta);
}