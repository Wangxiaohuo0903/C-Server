#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <chrono>

class ThreadPool {
public:
    ThreadPool(size_t minThreads, size_t maxThreads, std::chrono::milliseconds idleTime = std::chrono::milliseconds(10000))
        : stop(false), minThreads(minThreads), maxThreads(maxThreads), idleTime(idleTime), activeThreads(0) {
        workers.reserve(maxThreads);
        for (size_t i = 0; i < minThreads; ++i) {
            addThread();
        }
        // 启动后台管理线程，用于调整线程池中的线程数量
        managementThread = std::thread(&ThreadPool::manageThreads, this);
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
        if (managementThread.joinable()) {
            managementThread.join();
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

            tasks.emplace_back([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop;
    std::atomic<size_t> minThreads, maxThreads;
    std::chrono::milliseconds idleTime;
    std::atomic<size_t> activeThreads;
    std::thread managementThread;

    void addThread() {
        workers.emplace_back([this] {
            for(;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    this->condition.wait(lock, [this]{
                        return this->stop || !this->tasks.empty();
                    });
                    if(this->stop && this->tasks.empty()) return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop_front();
                }
                task();
            }
        });
        ++activeThreads;
    }

    // 后台管理线程函数，用于动态调整线程数量
    void manageThreads() {
        while (!stop) {
            std::this_thread::sleep_for(idleTime);
            std::unique_lock<std::mutex> lock(queue_mutex);
            // 根据当前任务数量和活跃线程数动态增减线程
            if (tasks.empty() && activeThreads > minThreads) {
                --activeThreads; // 减少一个线程
                condition.notify_one(); // 通知一个线程退出
                continue;
            }
            if (tasks.size() > activeThreads && activeThreads < maxThreads) {
                addThread(); // 增加一个新线程
            }
        }
    }
};
