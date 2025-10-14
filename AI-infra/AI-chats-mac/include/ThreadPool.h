#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

// 简化的线程池实现 (Mac 兼容版本)
class ThreadPool {
public:
    // 构造函数
    ThreadPool(size_t minThreads, size_t maxThreads)
        : stop(false) {
        // 创建初始线程
        for (size_t i = 0; i < minThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });

                        if (this->stop && this->tasks.empty())
                            return;

                        if (!this->tasks.empty()) {
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }
                    }

                    if (task) {
                        task();
                    }
                }
            });
        }
    }

    // 析构函数
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    // 提交任务到线程池
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;              // 工作线程
    std::queue<std::function<void()>> tasks;       // 任务队列

    std::mutex queueMutex;                         // 队列互斥锁
    std::condition_variable condition;             // 条件变量
    std::atomic<bool> stop;                        // 停止标志
};
