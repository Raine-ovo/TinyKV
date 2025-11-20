#ifndef LOCKQUEUE_H
#define LOCKQUEUE_H

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <vector>

// 该队列会被 muduo 库中或其他的多线程进行写入 msg ，还要被写日志线程写入文件，
// 因此需要实现线程安全

// 异步双缓冲队列
// 前台写线程将日志写入前台缓冲区，后台读线程把后台缓冲区 swap 出来批量写文件
// 这样全程只需要对 swap 上锁
template<typename T>
class LockQueue
{
public:
    using buffer_ptr = std::unique_ptr<std::vector<T>>;

    LockQueue(): _front(std::make_unique<std::vector<T>>()),
                 _back(std::make_unique<std::vector<T>>()) {}
    
    // 生产者
    void push(const T& msg) { append(msg); }
    void push(T&& msg) { append(std::move(msg)); }

    // 消费者
    std::vector<T> pop()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this] { return !_front->empty() || _stop; });
        return swap_buffer(lock);
    }

    std::vector<T> try_pop(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        bool ok = _cond.wait_for(lock, timeout, [this] { return !_front->empty() || _stop; });
        
        // 超时且没有数据
        if (!ok && _front->empty()) return {};

        return swap_buffer(lock);
    }

    // 工具函数
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _front->size() + _back->size();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _front->empty() && _back->empty();
    }

    void shutdown()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _cond.notify_all();
    }


private:
/*
    使用 typename U 对 append 不同实参类型进行再次推导
    因为模板实例化后 T 的类型就定好了，而 append 实参有右值和左值，所以要重新推导确认实参类型
*/
    template <typename U>
    void append(U&& msg)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _front->emplace_back(std::forward<U>(msg));
            if (_front->size() < kBatchSize) return ;
            // 前台缓冲区满了，swap到后台缓冲区
            _front.swap(_back);
        }
        _cond.notify_one(); // 唤醒刷盘线程
    }

    std::vector<T> swap_buffer(std::unique_lock<std::mutex>& lock)
    {
        // 调用时已经上锁了，直接换走 back
        std::cout << "front size: " << _front->size()
            << "\nback size: " << _back->size() << std::endl;
        _front.swap(_back);
        auto ret = std::move(*_back);
        _back->clear();
        return ret;
    }

private:
    static constexpr size_t kBatchSize = 4 * 1024;
    mutable std::mutex _mutex;
    std::condition_variable _cond;

    buffer_ptr _front;
    buffer_ptr _back;
    bool _stop{false};
};

#endif