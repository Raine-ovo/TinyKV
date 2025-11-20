#ifndef APPLYCHAN_H
#define APPLYCHAN_H

#include <queue>
#include <mutex>
#include <condition_variable>

// 该队列会被多线程进行写入 msg ，还要被读线程写应用。
// 因此需要实现线程安全

// 为了防止写线程频繁在空队列时进行加锁和释放锁带来的性能损耗，这里选择
// 在 pop 时阻塞，当遇到其他线程 push msg 时才唤醒，使用条件变量实现
template<typename T>
class ApplyChan
{
public:
    void push(const T &msg)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(msg);
        _cond.notify_one(); // 因为只有一个磁盘 I/O 线程，只需要唤醒一个
    }

    // 移动语义版本的 push
    void push(T&& msg)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _queue.push(msg);
        _cond.notify_one(); // 因为只有一个磁盘 I/O 线程，只需要唤醒一个
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        while (_queue.empty())
        {
            _cond.wait(lock); // 释放锁并进行阻塞状态
        }
        // 唤醒，此时有锁，并进行 pop
        T msg = _queue.front();
        _queue.pop();
        return msg;
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

    bool empty()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _queue.empty();
    }

private:
    std::queue<T> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
};

#endif