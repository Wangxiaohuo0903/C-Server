#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <future>
#include <condition_variable>
#include <random>
#include <chrono>
#include <iostream>

/**
 * 优化版： 
 * 1) 每个线程有自己的任务队列 + 条件变量 
 * 2) 一个简单的“管理线程”周期性监控负载，并根据 min/max 伸缩 
 * 3) 不再使用全局 stop 去减少线程，改用标记某线程 needExit 
 * 4) 任务默认放到调用线程对应的队列，否则随机选一个
 */
class ThreadPool {
public:
    ThreadPool(size_t minThreads, size_t maxThreads)
        : minThreads_(minThreads), 
          maxThreads_(maxThreads), 
          globalStop_(false)
    {
        // 先确保 minThreads_ <= maxThreads_
        if (minThreads_ > maxThreads_) {
            throw std::runtime_error("minThreads cannot exceed maxThreads");
        }
        // 创建管理线程
        managerThread_ = std::thread(&ThreadPool::manageThreadFunc, this);
        // 初始化时，先启动 minThreads 个工作线程
        addWorkers(minThreads_);
    }

    ~ThreadPool() {
        // 通知所有线程和管理线程停止
        {
            std::unique_lock<std::mutex> lk(managerMutex_);
            globalStop_ = true;
        }
        managerCv_.notify_one();
        if (managerThread_.joinable()) {
            managerThread_.join();
        }

        // 让所有工作线程退出
        for (auto &w : workers_) {
            {
                std::unique_lock<std::mutex> lk(w->mtx);
                w->shouldRun = false;
            }
            w->cv.notify_one();
        }

        // 等待所有工作线程结束
        for (auto &w : workers_) {
            if (w->th.joinable()) {
                w->th.join();
            }
        }
    }

    // 提交任务 (简化：默认把任务放到当前线程对应队列；若当前线程不在池内，就随机放一个队列)
    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::function<void()> wrapper = [task]() { (*task)(); };
        // 找到“当前线程”在池中的索引
        size_t idx = getThreadIndex();
        if (idx == (size_t)-1) {
            // 当前线程不在池中, 随机放到一个队列
            std::uniform_int_distribution<size_t> dist(0, workers_.size()-1);
            static thread_local std::mt19937 rng{std::random_device{}()};
            idx = dist(rng);
        }
        {
            std::unique_lock<std::mutex> lk(workers_[idx]->mtx);
            workers_[idx]->tasks.emplace_back(std::move(wrapper));
        }
        workers_[idx]->cv.notify_one();

        return task->get_future();
    }

private:
    // 工作线程结构
    struct Worker {
        std::thread th;
        std::deque<std::function<void()>> tasks;
        std::mutex mtx;              
        std::condition_variable cv;
        std::atomic<bool> shouldRun{true};  // 是否继续运行

        // 每个线程一个 ID, 以便 getThreadIndex() 识别
        size_t workerId;
    };

    // ====================== 核心成员 ======================
    std::vector<std::unique_ptr<Worker>> workers_;  // 工作线程列表
    std::thread managerThread_;                     // 管理线程
    std::mutex managerMutex_;
    std::condition_variable managerCv_;
    std::atomic<bool> globalStop_;                  // 全局停止标志

    size_t minThreads_;
    size_t maxThreads_;

    // ====================== 工作线程函数 ======================
    void workerFunc(Worker* w) {
        // 在线程局部存个 ID
        threadId_ = w->workerId;
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(w->mtx);
                // 等待有任务 或 shouldRun=false
                w->cv.wait(lk, [&] {
                    return !w->tasks.empty() || !w->shouldRun.load();
                });
                if (!w->shouldRun.load() && w->tasks.empty()) {
                    // 线程被要求停止且无任务可做 => 退出
                    return;
                }
                if (!w->tasks.empty()) {
                    task = std::move(w->tasks.front());
                    w->tasks.pop_front();
                }
            }
            // 真正执行任务(若 task 不为空)
            if (task) {
                try {
                    task();
                } catch (...) {
                    // 如果任务抛异常，最好在此捕获并记录，避免线程直接结束
                    // 例如： std::cerr << "Task threw an exception!\n";
                }
            }
        }
    }

    // ====================== 管理线程：周期性扩缩 ======================
    void manageThreadFunc() {
        using namespace std::chrono_literals;
        while (true) {
            // 等待一段时间 或 直到 globalStop_
            {
                std::unique_lock<std::mutex> lk(managerMutex_);
                managerCv_.wait_for(lk, 500ms, [&] { return globalStop_.load(); });
                if (globalStop_.load()) {
                    return; // 全局停止, 退出管理线程
                }
            }

            // 周期性检查负载
            adjustThreadCount();
        }
    }

    // 根据当前队列总长度、线程数，来扩或缩
    void adjustThreadCount() {
        if (globalStop_.load()) return;

        // 统计总任务数
        size_t totalTasks = 0;
        for (auto &w : workers_) {
            std::lock_guard<std::mutex> lk(w->mtx);
            totalTasks += w->tasks.size();
        }
        size_t currentNum = workers_.size();

        // 简单策略：如果总任务 > 当前线程数，就尝试加线程；如果总任务 < 当前线程数/2，就尝试减线程
        // （仅作示例，实际需更精细的阈值）
        if (totalTasks > currentNum && currentNum < maxThreads_) {
            size_t toAdd = std::min((size_t)(totalTasks - currentNum), maxThreads_ - currentNum);
            // 最多一次性加多少可再行限制
            addWorkers(toAdd);
        } else if (totalTasks < currentNum / 2 && currentNum > minThreads_) {
            size_t toRemove = std::min(currentNum - minThreads_, currentNum/2); 
            removeWorkers(toRemove);
        }
    }

    // ====================== 添加/移除线程 ======================
    void addWorkers(size_t n) {
        for (size_t i = 0; i < n; i++) {
            auto w = std::make_unique<Worker>();
            w->workerId = workers_.size(); 
            w->th = std::thread(&ThreadPool::workerFunc, this, w.get());
            workers_.push_back(std::move(w));
        }
    }

    void removeWorkers(size_t n) {
        if (n >= workers_.size()) {
            n = workers_.size() - minThreads_;  // 保证不低于 minThreads_
        }
        // 这里简单从最后往前移除 n 个
        for (size_t i = 0; i < n; i++) {
            auto &w = workers_.back();
            {
                std::unique_lock<std::mutex> lk(w->mtx);
                w->shouldRun = false; // 让它退出
            }
            w->cv.notify_one();
            if (w->th.joinable()) {
                w->th.join();
            }
            workers_.pop_back();
        }
    }

    // ====================== 辅助：获取当前线程在池中的索引 ======================
    // 这里用 thread_local 存线程ID；如果不是池内线程，返回 -1
    static thread_local size_t threadId_;
    size_t getThreadIndex() {
        // 如果 threadId_ 超过 workers_.size() 或者查不到，则说明是外部线程
        if (threadId_ < workers_.size()) {
            return threadId_;
        }
        return (size_t)-1;
    }
};

// static 成员需在类外定义
thread_local size_t ThreadPool::threadId_ = (size_t)-1;
