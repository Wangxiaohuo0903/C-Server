#include <vector> // 引入向量容器，用于存储工作线程
#include <queue>  // 引入队列容器，用于存储待执行的任务
#include <thread> // 引入线程库，用于创建和管理线程
#include <mutex>  // 引入互斥锁，用于保护共享数据的访问
#include <condition_variable> // 引入条件变量，用于线程间的同步
#include <functional> // 引入函数对象库，用于存储任意类型的可调用对象
#include <future> // 引入未来库，用于异步获取任务执行结果
#include <stdexcept> // 引入标准异常库，用于处理异常情况

// 线程池类定义
class ThreadPool {
public:
    // 构造函数，接收线程数量和最大队列大小为参数
    ThreadPool(size_t threads, size_t maxQueueSize = 100) // 默认最大队列大小为100
    : stop(false), maxQueueSize(maxQueueSize) {
        // 创建指定数量的工作线程
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        // 锁定互斥锁，保护共享数据tasks的访问
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        // 等待直到有任务到来或者线程池停止
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        // 如果线程池停止且任务队列为空，则退出循环，结束线程
                        if(this->stop && this->tasks.empty()) return;
                        // 从任务队列中取出一个任务
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    // 执行取出的任务
                    task();
                    // 任务完成后，通知可能在等待任务队列不满的线程
                    this->condition.notify_one();
                }
            });
        }
    }

    // 提交任务到线程池的模板方法
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        // 使用std::result_of获取函数f的返回类型
        using return_type = typename std::result_of<F(Args...)>::type;

        // 创建一个packaged_task，它将包装提交的任务
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        // 获取与packaged_task关联的future对象，用于返回任务执行结果
        std::future<return_type> res = task->get_future();

        {
            // 锁定互斥锁，保护共享数据tasks的访问
            std::unique_lock<std::mutex> lock(queue_mutex);

            // 如果任务队列已满，则抛出异常
            if (tasks.size() >= maxQueueSize) {
                throw std::runtime_error("enqueue failed: ThreadPool queue is full");
            }

            // 如果线程池已停止，则抛出异常
            if(stop) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            // 将任务添加到任务队列
            tasks.emplace([task](){ (*task)(); });
        }

        // 通知一个等待中的线程，有新的任务到来
        condition.notify_one();
        // 返回任务的future对象
        return res;
    }

    // 析构函数，负责停止线程池并等待所有工作线程结束
    ~ThreadPool() {
        {
            // 锁定互斥锁，设置停止标志为true
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        // 唤醒所有等待中的线程
        condition.notify_all();
        // 等待所有工作线程结束
        for(std::thread &worker: workers) {
            worker.join();
        }
    }

private:
    // 存储工作线程的向量
    std::vector<std::thread> workers;
    // 任务队列
    std::queue<std::function<void()>> tasks;
    // 用于保护共享数据tasks的互斥锁
    std::mutex queue_mutex;
    // 条件变量，用于线程间的同步
    std::condition_variable condition;
    // 停止标志
    bool stop;
    // 最大队列长度
    size_t maxQueueSize;
};


