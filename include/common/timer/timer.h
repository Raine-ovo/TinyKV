#ifndef TIMER_H
#define TIMER_H

#include <functional>
#include <cstdint>
#include <thread>
#include <memory>

// 实现一个可以支持回调函数的定时器
class Timer
{
public:
    using TimerCallback = std::function<void()>;

    // timeout 定时时间(ms)
    // callback 回调函数
    Timer(uint timeout = 1000, TimerCallback  callback = []() {});
    ~Timer();

    static uint random_time(const uint &begin, const uint &end);
    void reset(uint timeout = 1000); // 重置时间 timeout
    void random_reset(const uint &begin, const uint &end);
    
    void setCallback(TimerCallback callback);
    void run(); // 启用线程

    void stop(); // 停止计时器
    void kill();

private:
    TimerCallback _callback;

    uint _timeout;
    // 为了实现在中途 reset ，不能直接让线程 sleep timeout 时间
    // 这里的做法是每次 sleep 1ms 然后检查
    uint _runningtime; 

    // 底层线程, 因为创建线程后就直接跑了，为了在 run 后跑，需要用指针
    std::shared_ptr<std::thread> _thread;

    bool is_stopped; 
    bool is_killed; // 不需要计时器了，直接结束以便析构
};

#endif
