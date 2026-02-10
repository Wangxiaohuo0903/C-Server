// 引入必要的头文件
#include <vector> // 使用vector来存储工作线程和本地任务队列
#include <deque> // 使用deque作为任务队列，支持从两端操作
#include <thread> // 使用thread来创建和管理线程
#include <mutex> // 使用mutex来保护共享资源，防止数据竞争
#include <condition_variable> // 使用condition_variable来同步线程，实现等待唤醒机制
#include <functional> // 使用functional来处理任务函数
#include <future> // 使用future来获取异步操作的结果
#include <atomic> // 使用atomic来实现原子操作，保证操作的原子性
#include <chrono> // 使用chrono来处理时间
#include <random> // 使用random来实现随机数生成，用于任务窃取算法

// 定义线程池类
class ThreadPool {
public:
    // 构造函数，初始化线程池的最小和最大线程数
    ThreadPool(size_t minThreads, size_t maxThreads)
        : stop(false), minThreads(minThreads), maxThreads(maxThreads), activeThreads(0) {
        // 预分配本地任务队列和互斥锁
        localTasks.resize(maxThreads);
        localMutexes.resize(maxThreads);
        for (size_t i = 0; i < maxThreads; ++i) {
            localMutexes[i] = std::make_unique<std::mutex>();
        }
        // 启动初始工作线程
        addWorkers(minThreads);
    }

    // 析构函数，负责清理资源，停止所有工作线程
    ~ThreadPool() {
        stopPool();
    }

    // 提交任务到线程池
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        // 创建任务包装对象
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // 获取任务的future对象
        std::future<return_type> res = task->get_future();
        // 分配任务到最少负载的线程
        size_t leastLoadedThread = selectLeastLoadedThread();
        {
            std::lock_guard<std::mutex> lock(*localMutexes[leastLoadedThread]);
            localTasks[leastLoadedThread].emplace_back([task]() { (*task)(); });
        }
        // 通知一个可能在等待的线程
        condition.notify_one();
        // 动态调整线程池大小
        adjustThreadPoolSize();
        return res;
    }

private:
    // 私有成员变量
    std::vector<std::thread> workers; // 工作线程集合
    std::vector<std::unique_ptr<std::mutex>> localMutexes; // 每个本地任务队列对应的互斥锁
    std::vector<std::deque<std::function<void()>>> localTasks; // 本地任务队列集合
    std::condition_variable condition; // 条件变量，用于线程同步
    std::atomic<bool> stop; // 原子变量，标记线程池是否停止
    size_t minThreads, maxThreads; // 最小和最大线程数
    std::atomic<size_t> activeThreads; // 当前活跃的线程数
    
    // 添加工作线程到线程池
    void addWorkers(size_t numberOfWorkers) {
        // 循环创建指定数量的工作线程
        for (size_t i = 0; i < numberOfWorkers; ++i) {
            // 向工作线程集合中添加新线程
            workers.emplace_back([this, i] {
                while (true) {
                    std::function<void()> task;
                    {
                        // 对当前线程的本地互斥锁上锁
                        std::unique_lock<std::mutex> lock(*this->localMutexes[i]);
                        // 等待直到有任务可执行或线程池停止
                        this->condition.wait(lock, [this, i, &task] {
                            // 如果设置了停止标志、本地任务队列非空、或成功从其他线程窃取任务，则继续
                            return this->stop || !this->localTasks[i].empty() || tryStealTask(i, task);
                        });
                        // 如果线程池停止且当前线程的任务队列为空，且没有从其他线程窃取到任务，则退出循环
                        if (this->stop && this->localTasks[i].empty() && !task) return;
                        // 如果未从其他线程窃取到任务，且本地任务队列非空，则从本地队列取出任务执行
                        if (!task && !this->localTasks[i].empty()) {
                            task = std::move(this->localTasks[i].front());
                            this->localTasks[i].pop_front();
                        }
                    }
                    // 执行获取到的任务
                    if (task) task();
                }
            });
            // 活跃线程数增加
            activeThreads.fetch_add(1);
        }
    }

    // 尝试从其他线程的本地任务队列中窃取任务
    bool tryStealTask(size_t thiefIndex, std::function<void()>& task) {
        for (size_t i = 0; i < localTasks.size(); ++i) {
            if (i != thiefIndex && !localTasks[i].empty()) {
                // 上锁目标线程的互斥锁
                std::lock_guard<std::mutex> lock(*localMutexes[i]);
                if (!localTasks[i].empty()) {
                    // 从任务队列尾部取出任务执行，实现窃取
                    task = std::move(localTasks[i].back());
                    localTasks[i].pop_back();
                    return true; // 窃取成功
                }
            }
        }
        return false; // 窃取失败
    }

    // 选择最少负载的线程
    size_t selectLeastLoadedThread() {
        size_t leastLoaded = 0;
        size_t minSize = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < localTasks.size(); ++i) {
            std::lock_guard<std::mutex> lock(*localMutexes[i]);
            if (localTasks[i].size() < minSize) {
                leastLoaded = i;
                minSize = localTasks[i].size();
            }
        }
        return leastLoaded;// 返回最少负载的线程索引
    }

    // 动态调整线程池大小
    void adjustThreadPoolSize() {
        if (stop) return; // 如果线程池已经停止，则不进行调整
        size_t currentSize = workers.size();
        // 计算期望的线程数量
        size_t desiredSize = std::min(std::max(minThreads, activeThreads.load()), maxThreads);
        // 如果当前线程数少于期望线程数，则增加线程
        if (currentSize < desiredSize) {
            addWorkers(desiredSize - currentSize);
        } else if (currentSize > desiredSize) {
            // 如果当前线程数多于期望线程数，则减少线程
            reduceWorkers(currentSize - desiredSize);
        }
    }

    /*
    // 动态调整线程池大小
    void adjustThreadPoolSize() {
        if (stop) return; // 如果线程池已经停止，则不进行调整

        // 获取当前任务队列的总任务数
        size_t totalTasks = 0;
        for (const auto& queue : localTasks) {
            std::lock_guard<std::mutex> lock(*localMutexes[queue.first]);
            totalTasks += queue.second.size();
        }

        // 获取当前活跃（工作中或可工作）的线程数
        size_t currentActiveThreads = activeThreads.load();

        // 根据当前任务量和活跃线程数确定是否需要调整线程数
        if (totalTasks > currentActiveThreads && workers.size() < maxThreads) {
            // 如果任务多于活跃线程数且当前线程总数小于最大限制，则增加线程
            size_t threadsToAdd = std::min(maxThreads - workers.size(), totalTasks - currentActiveThreads);
            addWorkers(threadsToAdd);
        } else if (totalTasks < currentActiveThreads / 2 && workers.size() > minThreads) {
            // 如果任务数少于活跃线程数的一半且当前线程总数大于最小限制，则减少线程
            size_t threadsToRemove = std::min(workers.size() - minThreads, currentActiveThreads - totalTasks / 2);
            reduceWorkers(threadsToRemove);
        }
    }

    
    */

    // 减少工作线程
    void reduceWorkers(size_t count) {
        for (size_t i = 0; i < count; ++i) {
            stopWorker();
        }
    }


    // 停止一个工作线程
    void stopWorker() {
        stop = true; // 临时设置停止标志以终止一个线程
        condition.notify_one(); // 通知一个等待的线程
        if (workers.back().joinable()) {
            workers.back().join(); // 等待最后一个线程完成
            workers.pop_back(); // 从工作线程列表中移除
            activeThreads.fetch_sub(1); // 活跃线程数减少
        }
        stop = false; // 重置停止标志，以便其它线程继续工作
    }

    // 停止线程池中所有线程的工作
    void stopPool() {
        stop = true; // 设置停止标志
        condition.notify_all(); // 唤醒所有等待的线程
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join(); // 等待每个线程完成
        }
    }
    
};

