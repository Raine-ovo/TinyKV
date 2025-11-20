#ifndef LOGGER_H
#define LOGGER_H

#include "common/lockqueue/lockqueue.h"
#include "storage/raft/log_manager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>
#include <thread>
#include <semaphore>
#include <format>

enum class LogLevel: std::uint8_t
{
    INFO,
    ERROR,
    FATAL,
    DEBUG,
    WARNING,
};

// 实现异步缓冲队列日志系统
// 为了防止写日志线程频繁获取和释放锁消耗性能，选择使用条件变量机制

/*
┌-------------┐      ┌-------------┐      ┌-------------┐
│ 任意线程写   │----►│ 前台 buffer │      │ 后台 buffer │◄--┐
│             │     │  （lock-free │◄----►│  （单线程） │   │
└-------------┘     │   多写者）   │      │             │   │
                    └-------------┘      └-------------┘   │
                          ▲                                │
                          │ notify_one                     │ pop
                          ▼                                │
                    ┌-------------┐      ┌-------------┐   │
                    │ 后台线程池   │      │ 磁盘文件     │   │
                    │ （可配置 N） │----►│ 日期+大小切  │   │
                    └-------------┘      └-------------┘   │
*/

// 日志条目
struct LogEntry
{
    std::chrono::system_clock::time_point tp;
    LogLevel lvl;
    std::string msg;
};

// 异步日志
class Logger
{
public:
    static Logger& getInstance();

    // 模板参数推导 ~= template<typename Args...>
    void log(LogLevel lvl, std::string_view fmt, auto&&...args)
    {
        set_level(lvl);
        std::string msg = std::vformat(fmt, std::make_format_args(args...));
        _queue.push(LogEntry{std::chrono::system_clock::now(), lvl, std::move(msg)});
        std::cout << "LOG 写入数据了" << std::endl;
    }

    // 设置 log 级别
    void set_level(LogLevel l);
    // 日志文件存储路径
    void set_log_dir(std::string dir);
    // 关闭日志系统
    void shutdown();

private:
    // 日志系统只需要一个就行，设计为单例模式
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;

    void backend(); // 后台程序主循环

private:
    std::atomic<LogLevel> _min_level{LogLevel::INFO};
    std::string _log_dir{"../log"};
    LockQueue<LogEntry> _queue;
    std::jthread _worker;
};

#define LOG_INFO(...) Logger::getInstance().log(LogLevel::INFO, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::getInstance().log(LogLevel::DEBUG, __VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().log(LogLevel::ERROR, __VA_ARGS__)
#define LOG_FATAL(...) \
    do { \
        Logger::getInstance().log(LogLevel::FATAL, __VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while(0);


// #define LOG_BASE(level, fmt, ...) \
//     do \
//     { \
//         Logger &logger = Logger::getInstance(); \
//         logger.setLogLevel(level); \
//         std::string msg = std::format("[{}]" fmt, \
//             logger.get_prefix(), ##__VA_ARGS__); \
//         Logger::getInstance().log(msg); \
//     } while(0);

// #define LOG_INFO(fmt, ...) LOG_BASE(LogLevel::INFO, fmt, ##__VA_ARGS__)
// #define LOG_ERROR(fmt, ...) LOG_BASE(LogLevel::ERROR, fmt, ##__VA_ARGS__)
// #define LOG_DEBUG(fmt, ...) LOG_BASE(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
// #define LOG_FATAL(fmt, ...) \
//     do \
//     { \
//         LOG_BASE(LogLevel::FATAL, fmt, ##__VA_ARGS__); \
//         exit(EXIT_FAILURE); \
//     } while(0);
    
#endif