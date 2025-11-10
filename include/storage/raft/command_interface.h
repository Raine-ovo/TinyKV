#ifndef COMMAND_INTERFACE_H
#define COMMAND_INTERFACE_H

// 使用继承 + 多态实现命令接口
#include "proto/command.pb.h"
#include <memory>
#include <string>
class Command
{
public:
    virtual ~Command() = default;

    // 序列化为 protobuf
    virtual ::command::Command to_protobuf() const = 0;

    // 从 protobuf 反序列化
    virtual void from_protobuf(const ::command::Command& proto_cmd) = 0;

    virtual std::unique_ptr<Command> clone() const = 0;

    // 获取命令类型
    virtual std::string type_name() const = 0;

private:
};

class PutCommand : public Command
{
public:
    PutCommand() = default;
    PutCommand(const std::string& key, const std::string& value);
    ::command::Command to_protobuf() const;
    void from_protobuf(const ::command::Command& proto_cmd);
    std::unique_ptr<Command> clone() const;
    std::string type_name() const;
private:
    std::string _key;
    std::string _value;
};

class GetCommand : public Command
{
public:
    GetCommand() = default;
    GetCommand(const std::string& key);
    ::command::Command to_protobuf() const;
    void from_protobuf(const ::command::Command& proto_cmd);
    std::unique_ptr<Command> clone() const;
    std::string type_name() const;
private:
    std::string _key;
};

class DeleteCommand : public Command
{
public:
    DeleteCommand() = default;
    DeleteCommand(const std::string& key);
    ::command::Command to_protobuf() const;
    void from_protobuf(const ::command::Command& proto_cmd);
    std::unique_ptr<Command> clone() const;
    std::string type_name() const;
private:
    std::string _key;
};

class IncrementCommand : public Command
{
public:
    IncrementCommand() = default;
    IncrementCommand(const std::string& key, int delta);
    ::command::Command to_protobuf() const;
    void from_protobuf(const ::command::Command& proto_cmd);
    std::unique_ptr<Command> clone() const;
    std::string type_name() const;
private:
    std::string _key;
    int _delta;
};

// 使用命令工厂模式创造命令
class CommandFactory
{
public:
    static std::unique_ptr<Command> create_from_protobuf(const ::command::Command& proto_cmd);
    static std::unique_ptr<Command> create_put(const std::string& key, const std::string& value);
    static std::unique_ptr<Command> create_get(const std::string& key);
    static std::unique_ptr<Command> create_delete(const std::string& key);
    static std::unique_ptr<Command> create_increament(const std::string& key, int delta);
private:
};

#endif