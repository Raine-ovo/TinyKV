#include "common/timer/timer.h"

#include <random>
#include <chrono>

Timer::Timer(uint timeout, TimerCallback callback)
    : _timeout(timeout), _callback(callback), is_stopped(false),
      _runningtime(0)
{
}

Timer::~Timer()
{
    if (_thread != nullptr)
    {
        // 如果对应线程还在执行，那就等待执行完毕后回收资源
        if (_thread->joinable())
        {
            _thread->join();
        }
    }
}

uint Timer::random_time(const uint &begin, const uint &end)
{
    if (begin >= end) return 0;

    unsigned long long seed = std::chrono::steady_clock::now().time_since_epoch().count();
    std::mt19937 engine(seed);
    std::uniform_int_distribution<int> dist(begin, end);
    
    return dist(engine);
}

void Timer::reset(uint timeout)
{
    _timeout = timeout;
    _runningtime = 0;
}

void Timer::setCallback(TimerCallback callback)
{
    _callback = callback;
}

void Timer::run()
{
    _thread = std::make_shared<std::thread>([&]() {
        while (true)
        {
            if (is_stopped) {
                continue;
            }

            // sleep 1ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++ _runningtime;

            if (_runningtime >= _timeout)
            {
                _callback();
                return ;
            }
        }
    });
}

void Timer::stop()
{
    is_stopped = true;
}