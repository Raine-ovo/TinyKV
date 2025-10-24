#ifndef LOCKQUEUE_H
#define LOCKQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>

// 该队列会被 muduo库中或其他的多线程进行写入 msg ，还要被写日志线程写入文件，
// 因此需要实现线程安全

// 为了防止日志系统的写线程频繁在空队列时进行加锁和释放锁带来的性能损耗，这里选择
// 在 pop 时阻塞，当遇到其他线程 push msg 时才唤醒，使用条件变量实现
template<typename T>
class LockQueue
{
public:
    void push(const T &msg)
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

private:
    std::queue<T> _queue;
    std::mutex _mutex;
    std::condition_variable _cond;
};

#endif