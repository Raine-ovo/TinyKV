#include "common/logger/logger.h"

#include <ctime>
#include <string>
#include <iostream>
#include <fstream>

const std::string Logger::get_prefix()
{
    if (_log_level == LogLevel::INFO) return "INFO";
    else if (_log_level == LogLevel::DEBUG) return "DEBUG";
    else if (_log_level == LogLevel::ERROR) return "ERROR";
    else if (_log_level == LogLevel::FATAL) return "FATAL";

    return "";
}

Logger& Logger::getInstance()
{
    static Logger logger;
    return logger;
}

Logger::Logger()
{
    _log_level = LogLevel::INFO;
    _log_thread = std::make_unique<std::thread>([&]() {
        while (true) 
        {
            // 组装格式: time [INFO] msg
            // 年/月/日 文件名，时/分/秒 条目
            time_t now = time(nullptr);
            tm *nowtm = localtime(&now);
            
            std::string file_name = std::format("{}-{}-{}.txt", 
                nowtm->tm_year+1900, nowtm->tm_mon+1, nowtm->tm_mday);
            // 放在 log 文件夹下
            file_name.insert(0, "../log/");

            // msg 内容
            std::string msg = _queue.pop();
            std::string format_msg = std::format("{:2>0}:{:2>0}:{:2>0} => {}",
                nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec, msg);
                
            // 打开文件，追加写
            std::ofstream outfile;
            outfile.open(file_name.c_str(), std::ios::app);
            outfile << format_msg << '\n';
            outfile.close();
        }
    });
}

Logger::~Logger()
{
    if (_log_thread != nullptr)
    {
        if (_log_thread->joinable())
        {
            _log_thread->join();
        }
    }
}

void Logger::log(const std::string &msg)
{
    _queue.push(msg);
}

void Logger::setLogLevel(const LogLevel &level)
{
    _log_level = level;
}