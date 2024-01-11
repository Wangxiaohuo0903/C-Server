#include <vector>                    // 引入标准向量容器，用于存储工作线程
#include <queue>                     // 引入标准队列容器，用于存储待执行的任务
#include <thread>                    // 引入线程库，用于创建和管理线程
#include <mutex>                     // 引入互斥锁，用于确保线程安全
#include <condition_variable>        // 引入条件变量，用于线程等待和通知
#include <functional>                // 引入函数对象相关库，用于包装和执行任务
#include <future>                    // 引入future库，用于管理异步任务的结果


class ThreadPool {
public:
    // 构造函数，初始化线程池
    ThreadPool(size_t threads) : stop(false) {
        // 创建指定数量的工作线程
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        // 创建互斥锁以保护任务队列
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // 使用条件变量等待任务或停止信号
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        // 如果线程池停止且任务队列为空，线程退出
                        if(this->stop && this->tasks.empty()) return;
                        // 获取下一个要执行的任务
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    // 执行任务
                    task();
                }
            });
        }
    }

    // 将函数调用包装为任务并添加到任务队列，返回与任务关联的 future
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        // 创建共享任务包装器
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // 获取与任务关联的 future
        std::future<return_type> res = task->get_future();
        {
            // 使用互斥锁保护任务队列
            std::unique_lock<std::mutex> lock(queue_mutex);
            // 如果线程池已停止，则抛出异常
            if(stop) throw std::runtime_error("enqueue on stopped ThreadPool");
            // 将任务添加到队列
            tasks.emplace([task](){ (*task)(); });
        }
        // 通知一个等待的线程去执行任务
        condition.notify_one();
        return res;
    }

    // 析构函数，清理线程池资源
    ~ThreadPool() {
        {
            // 使用互斥锁保护停止标志
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        // 唤醒所有等待的线程
        condition.notify_all();
        // 等待所有工作线程退出
        for(std::thread &worker: workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;              // 存储工作线程
    std::queue<std::function<void()>> tasks;        // 存储任务队列
    std::mutex queue_mutex;                         // 任务队列的互斥锁
    std::condition_variable condition;              // 条件变量用于线程等待
    bool stop;                                      // 停止标志，控制线程池的生命周期
};