#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept> // 引入标准异常库

class ThreadPool {
public:
    ThreadPool(size_t threads, size_t maxQueueSize = 100) // 添加最大队列大小参数，默认为100
    : stop(false), maxQueueSize(maxQueueSize) {
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                    // 任务完成后通知，可能有线程等待队列不满
                    this->condition.notify_one();
                }
            });
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

        std::unique_lock<std::mutex> lock(queue_mutex);

        // 检查队列是否已满
        if (tasks.size() >= maxQueueSize) {
             LOG_ERROR("enqueue failed: ThreadPool queue is full");
            throw std::runtime_error("enqueue failed: ThreadPool queue is full");
        }

        if(stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }

        tasks.emplace([task](){ (*task)(); });
        
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers) {
            worker.join();
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    size_t maxQueueSize; // 最大队列长度
};
