#include "common/logger/logger.h"
#include "storage/raft/log_manager.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <string>
#include <iostream>
#include <fstream>
#include <string_view>
#include <ctime>
#include <thread>

static std::string level_str(LogLevel l)
{
    switch (l) {
        case LogLevel::INFO: return "INFO";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel:: FATAL: return "FATAL";
        case LogLevel::WARNING: return "WARNING";
    }
    return "UNK";
}

Logger::Logger()
    : _worker(&Logger::backend, this) {};

Logger::~Logger()
{
    shutdown();
}

Logger& Logger::getInstance()
{
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel l)
{
    _min_level = l;
}

void Logger::set_log_dir(std::string dir)
{
    _log_dir = std::move(dir);
}

void Logger::shutdown()
{
    _queue.shutdown();
}

void Logger::backend()
{
    auto open_file = [this](const std::tm& tm) -> std::ofstream
    {
        std::filesystem::create_directories(_log_dir);
        std::string name = std::format("{}/{:04d}-{:02d}-{:02d}.log",
                                        _log_dir, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return std::ofstream(name, std::ios::app);
    };

    std::ofstream out;
    std::time_t last_day = 0;
    std::tm tm_now{};
    
    while (true)
    {
        // DEBUG 阶段，这里先不用 try pop
        auto batch = _queue.pop();

        std::cout << "batch size: " << batch.size() << std::endl;
        for (const LogEntry& e: batch)
        {
            std::time_t t = std::chrono::system_clock::to_time_t(e.tp);
            
            // 日期切换 或 首次打开
            if (t / 86400 != last_day / 86400 || !out.is_open())
            {
                if (out.is_open())
                {
                    out.flush();
                    out.close();
                }
                
                localtime_r(&t, &tm_now);
                out = open_file(tm_now);
                last_day = t;
            }
            std::cout << "LOG写入文件了" << std::endl;
            out << std::format("{:02d}:{:02d}:{:02d} [{}] {}\n",
                                tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec,
                                level_str(e.lvl), e.msg);
        }

        out.flush();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (out.is_open())
    {
        out.flush();
        out.close();
    }
}