#ifndef LOGGER_H
#define LOGGER_H

#include "common/lockqueue/lockqueue.h"

#include <string>
#include <memory>
#include <thread>
#include <semaphore>
#include <format>

enum class LogLevel
{
    INFO,
    ERROR,
    FATAL,
    DEBUG,
};

// 实现异步缓冲队列日志系统
// 为了防止写日志线程频繁获取和释放锁消耗性能，选择使用条件变量机制
class Logger
{
public:
    static Logger& getInstance();

    const std::string get_prefix();
    void log(const std::string &msg);
    void setLogLevel(const LogLevel &level);

private:
    LogLevel _log_level;
    LockQueue<std::string> _queue;
    std::unique_ptr<std::thread> _log_thread;

    // 日志系统只需要一个就行，设计为单例模式
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
};

#define LOG_BASE(level, fmt, ...) \
    do \
    { \
        Logger &logger = Logger::getInstance(); \
        logger.setLogLevel(level); \
        std::string msg = std::format("[{}]" fmt, \
            logger.get_prefix(), ##__VA_ARGS__); \
        Logger::getInstance().log(msg); \
    } while(0);

#define LOG_INFO(fmt, ...) LOG_BASE(LogLevel::INFO, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE(LogLevel::ERROR, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_BASE(LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) \
    do \
    { \
        LOG_BASE(LogLevel::FATAL, fmt, ##__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while(0);
    
#endif