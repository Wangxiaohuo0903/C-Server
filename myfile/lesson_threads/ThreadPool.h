// 引入C++标准库中的向量容器，用于存储线程池相关的数据结构
#include <vector>

// 引入双端队列，用于存储待执行的任务
#include <deque>

// 引入库中的线程支持，创建和管理线程
#include <thread>

// 引入互斥量，用于保证多线程环境下资源访问的同步控制
#include <mutex>

// 引入条件变量，用于线程间的同步与通信
#include <condition_variable>

// 引入函数指针和模板元编程的支持，方便任务封装
#include <functional>

// 引入异步编程的支持，获取任务执行结果
#include <future>

// 引入原子操作，实现无锁计数器等并发控制
#include <atomic>

// 引入chrono库，进行时间间隔的处理
#include <chrono>

// 引入iostream，用于输出调试信息
#include <iostream>

// 引入可选类型，用于在任务窃取时返回可能为空的结果
#include <optional>

class ThreadPool {
public:
    // 构造函数，初始化线程池，指定最小线程数、最大线程数以及空闲超时时间
    ThreadPool(size_t minThreads, size_t maxThreads, std::chrono::milliseconds idleTime = std::chrono::milliseconds(10000))
        : stop(false), minThreads(minThreads), maxThreads(maxThreads), idleTime(idleTime), activeThreads(0) {
        
        // 初始化每个工作线程对应的本地任务队列
        localTasks.resize(maxThreads);
        
        // 创建并启动至少minThreads数量的工作线程
        for (size_t i = 0; i < minThreads; ++i) {
            addThread(i);
        }

        // 启动一个管理线程，负责动态调整工作线程数量
        managementThread = std::thread(&ThreadPool::manageThreads, this);
    }

    // 析构函数，在销毁线程池时等待所有工作线程结束并关闭管理线程
    ~ThreadPool() {
        stop = true;
        condition.notify_all();

        // 等待所有工作线程完成
        for(auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        // 关闭管理线程
        if (managementThread.joinable()) {
            managementThread.join();
        }
    }

    // 提交任务到线程池的方法，使用模板方法允许传入不同参数类型的函数对象
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {

        // 使用std::packaged_task封装任务，并获取其返回值的future
        using return_type = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> res = task->get_future();

        // 将任务添加到全局任务队列，并通知等待的线程有新任务到来
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            globalTasks.emplace_back([task]() { (*task)(); });
        }
        condition.notify_one();

        // 返回任务的未来结果
        return res;
    }

private:
    // 停止标志，用于通知所有线程停止工作
    std::atomic<bool> stop;

    // 最小和最大工作线程数
    size_t minThreads, maxThreads;

    // 工作线程空闲超时时间
    std::chrono::milliseconds idleTime;

    // 当前活跃的工作线程数（原子整型）
    std::atomic<size_t> activeThreads;

    // 保护任务队列的互斥量
    std::mutex queue_mutex;

    // 用于任务队列同步的条件变量
    std::condition_variable condition;

    // 每个工作线程拥有的本地任务队列
    std::vector<std::deque<std::function<void()>>> localTasks;

    // 全局共享的任务队列
    std::deque<std::function<void()>> globalTasks;

    // 工作线程集合
    std::vector<std::thread> workers;

    // 管理线程
    std::thread managementThread;

    // 添加一个新的工作线程，将线程放入workers集合中并启动
void addThread(size_t index) {
    workers.emplace_back([this, index] {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(this->queue_mutex);
                this->condition.wait_for(lock, this->idleTime, [this, index] {
                    return this->stop || !this->globalTasks.empty() || !this->localTasks[index].empty();
                });

                if (this->stop) return;

                if (!this->localTasks[index].empty()) {
                    task = std::move(this->localTasks[index].front());
                    this->localTasks[index].pop_front();
                } else if (!this->globalTasks.empty()) {
                    task = std::move(this->globalTasks.front());
                    this->globalTasks.pop_front();
                } else {
                    task = stealTask(index); // 尝试窃取任务
                    if (!task) continue; // 如果没有窃取到任务，继续等待
                }
            }
            if(task) {
                task();
            }
        }
    });
    ++activeThreads;
}


    // 任务窃取函数，从其他线程的本地任务队列尝试窃取一个任务
std::function<void()> stealTask(size_t thiefIndex) {
    for (size_t i = 0; i < localTasks.size(); ++i) {
        if (i != thiefIndex && !localTasks[i].empty()) {
            std::unique_lock<std::mutex> lock(this->queue_mutex, std::try_to_lock);
            if (lock.owns_lock() && !localTasks[i].empty()) {
                // 从队尾窃取任务
                auto task = std::move(localTasks[i].back());
                localTasks[i].pop_back();
                return task;
            }
        }
    }
    return nullptr;
}


    // 管理线程运行函数，负责根据当前任务负载动态调整工作线程数量
    void manageThreads() {
        while (!stop.load()) {
            std::this_thread::sleep_for(idleTime); // 定期检查线程池状态

            // 计算当前总的未完成任务数
            std::unique_lock<std::mutex> lock(queue_mutex);
            size_t currentTasks = globalTasks.size();
            for (auto& localQueue : localTasks) {
                currentTasks += localQueue.size();
            }
            size_t currentThreads = workers.size();
            lock.unlock();

            // 根据任务负载增加或减少线程
            if (currentTasks > currentThreads && currentThreads < maxThreads) {
                // 如果有未完成的任务且当前线程数小于最大线程数，则增加线程
                addThread(localTasks.size());
                localTasks.emplace_back(); // 为新线程分配新的本地任务队列
            } else if (currentTasks == 0 && currentThreads > minThreads) {
                // 如果没有待处理的任务且当前线程数大于最小线程数，则尝试减少线程
                reduceThreads();
            }
        }
    }

    // 函数用于减少工作线程的数量
    void reduceThreads() {
        // 设置局部停止标志，使得一个线程退出
        stop = true;
        condition.notify_one();

        // 重置stop标志，确保只让一个线程退出
        stop = false;
    }
};