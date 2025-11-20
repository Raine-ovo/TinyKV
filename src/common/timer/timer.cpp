#include "common/timer/timer.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

uint32_t Timer::random_time(const uint32_t& begin, const uint32_t& end)
{
    if (begin >= end) return begin;
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(begin, end);
    return dist(rng);
}

void Timer::start(const uint32_t& period_ms, bool forever, TimerCallBack callback, Mode mode)
{
    stop(); // 先停掉原本的线程
    _mode = mode;
    _period = Period(period_ms);
    _forever = forever;
    _callback = callback;

    _is_running = true;
    _td = std::make_shared<std::thread>([this] { worker(); });
}

void Timer::stop()
{
    if (_td != nullptr && _td->joinable())
    {
        _is_running = false;
        _cond.notify_one(); // 唤醒线程
        _td->join();
    }
}

void Timer::set_random(const uint32_t& begin, const uint32_t& end)
{
    random_begin = begin;
    random_end = end;
}

void Timer::reset(const uint32_t& new_period_ms)
{
    if (_mode == Mode::Random)
    {
        _period = Period(random_time(random_begin, random_end));
    }
    else
    {
        _period = Period(new_period_ms);
    }
    _resetting = true;
    _cond.notify_one();
}

void Timer::worker()
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point next = Clock::now() + _period.load();

    while (_is_running)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            if (_cond.wait_until(lock, next, [this] { return !_is_running; }))
                return ;
        }

        if (_resetting) _resetting = false;
        else if (_is_running) _callback();
        
        if (!_forever) 
        {
            _is_running = false;
            break;
        }
        next = Clock::now() + _period.load();
    }
}