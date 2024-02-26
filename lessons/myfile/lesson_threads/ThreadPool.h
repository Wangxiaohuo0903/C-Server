#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <chrono>
#include <iostream>

class ThreadPool {
public:
    ThreadPool(size_t minThreads, size_t maxThreads, std::chrono::milliseconds idleTime = std::chrono::milliseconds(10000))
        : stop(false), minThreads(minThreads), maxThreads(maxThreads), idleTime(idleTime), activeThreads(0) {
        workers.reserve(maxThreads);
        local_tasks.resize(maxThreads);
        for (size_t i = 0; i < minThreads; ++i) {
            addThread(i);
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if(worker.joinable()) {
                worker.join();
            }
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            if (stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            global_tasks.emplace_back([task]() { (*task)(); });

            // 动态增加线程的逻辑
            if (global_tasks.size() > activeThreads && activeThreads < maxThreads) { 
                addThread(activeThreads); // 传递新线程的索引
            }
        }
        condition.notify_one();
        return res;
    }

private:
    std::atomic<bool> stop;
    size_t minThreads, maxThreads;
    std::chrono::milliseconds idleTime;
    std::atomic<size_t> activeThreads;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::deque<std::function<void()>> global_tasks;
    std::vector<std::deque<std::function<void()>>> local_tasks;
    std::vector<std::thread> workers;

    void addThread(size_t workerId) {
        workers.emplace_back([this, workerId] {
            while (true) {
                bool hasTask = false;
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait_for(lock, this->idleTime, [this, workerId] {
                        return this->stop || !this->global_tasks.empty() || !this->local_tasks[workerId].empty();
                    });

                    if (this->stop && this->global_tasks.empty() && this->local_tasks[workerId].empty()) {
                        // 检查是否可以减少线程
                        if (activeThreads.load() > minThreads) {
                            --activeThreads;
                            return;
                        } else {
                            continue; // 继续等待任务或停止信号
                        }
                    }

                    if (!this->local_tasks[workerId].empty()) {
                        task = std::move(this->local_tasks[workerId].front());
                        this->local_tasks[workerId].pop_front();
                        hasTask = true;
                    } else if (!this->global_tasks.empty()) {
                        task = std::move(this->global_tasks.front());
                        this->global_tasks.pop_front();
                        hasTask = true;
                    }
                }

                if (hasTask) {
                    task();
                }
            }
        });

        ++activeThreads;
    }
};