#ifndef TIMER_H
#define TIMER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class Timer
{
public:
    using TimerCallBack = std::function<void()>;
    using Period = std::chrono::milliseconds;

    enum class Mode { Normal, Random };

    Timer() = default;
    ~Timer() { stop(); };

    void start(const uint32_t& period_ms, bool forever, TimerCallBack callback, Mode mode);
    void stop();
    
    static uint32_t random_time(const uint32_t& begin, const uint32_t& end);
    
    void set_random(const uint32_t& begin, const uint32_t& end);
    void reset(const uint32_t& new_period_ms);

private:
    std::shared_ptr<std::thread> _td; // 使用线程异步定时
    Mode _mode;
    std::atomic<Period> _period;
    std::atomic<bool> _is_running{false}; // 是否开始运行
    std::atomic<bool> _forever; // 循环定时
    std::atomic<bool> _resetting; // 现在状态是 reset
    TimerCallBack _callback;

    std::condition_variable _cond;
    std::mutex _mutex; // 保护 _cond

    uint32_t random_begin{400};
    uint32_t random_end{800};

private:
    void worker();
};

#endif