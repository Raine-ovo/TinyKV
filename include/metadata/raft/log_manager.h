#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>
#include <ranges>

struct Entry
{
    uint64_t term;
    std::string command;
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
    void drop(uint64_t index);

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