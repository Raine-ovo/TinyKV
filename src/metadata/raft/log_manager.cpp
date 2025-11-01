#include "metadata/raft/log_manager.h"
#include "common/logger/logger.h"
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <vector>

LogManager::LogManager()
    : lastIncludedIndex(0),
      lastIncludedTerm(0),
      entries(std::move(std::vector<Entry>(1, {0, ""})))
{
}

LogManager::~LogManager()
{}

void LogManager::reset()
{
    entries = std::move(std::vector<Entry>(1, {0, ""}));
}

Entry LogManager::get(uint64_t index)
{
    uint64_t log_index = index - lastIncludedIndex;
    if (log_index > entries.size() - 1)
    {
        LOG_ERROR("LogManager::get outbound!");
        exit(EXIT_FAILURE);
    }
    return entries[log_index];
}

void LogManager::put(const Entry& entry)
{
    entries.emplace_back(entry);
}

const std::vector<Entry>& LogManager::get_entries()
{
    return entries;
}

void LogManager::resize(uint64_t index)
{
    entries.resize(index);
}

void LogManager::drop(uint64_t index)
{
    uint64_t log_index = index - lastIncludedIndex;
    if (log_index < 0) return ;
    auto new_logs = entries | std::views::drop(index + 1);
    reset();
    std::ranges::copy(new_logs, std::back_inserter(entries));
}

uint64_t LogManager::lastIndex()
{
    return lastIncludedIndex + entries.size() - 1;
}

uint64_t LogManager::lastTerm()
{
    return entries.back().term;
}

void LogManager::set_lastIncludedIndex(uint64_t newnum)
{
    lastIncludedIndex = newnum;
}

void LogManager::set_lastIncludedTerm(uint64_t newnum)
{
    lastIncludedTerm = newnum;
}

uint64_t LogManager::get_lastIncludedIndex()
{
    return lastIncludedIndex;
}

uint64_t LogManager::get_lastIncludedTerm()
{
    return lastIncludedTerm;
}