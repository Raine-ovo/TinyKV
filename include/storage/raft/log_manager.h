#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "proto/command.pb.h"
#include <cstdint>
#include <string>
#include <vector>
#include <ranges>

struct Entry
{
    uint64_t term;
    ::command::Command command;
};

// 实现一个日志管理类
// 主要用于处理引入快照后的索引相关
class LogManager
{
public:
    LogManager();
    ~LogManager();

    void reset();

    Entry get(uint64_t index);
    void put(const Entry& entry);
    const std::vector<Entry>& get_entries();
    void resize(uint64_t index);
    // 丢弃前面的部分, 保留 [index+1, ...]
    // set 表示是否需要设置 lastIncludedIndex 和 lastIncludedTerm
    void drop(uint64_t index, bool set = false);

    uint64_t lastIndex();
    uint64_t lastTerm();

    void set_lastIncludedIndex(uint64_t newnum);
    void set_lastIncludedTerm(uint64_t newnum);
    uint64_t get_lastIncludedIndex();
    uint64_t get_lastIncludedTerm();

private:
    uint64_t lastIncludedIndex;
    uint64_t lastIncludedTerm;
    std::vector<Entry> entries;
};

#endif