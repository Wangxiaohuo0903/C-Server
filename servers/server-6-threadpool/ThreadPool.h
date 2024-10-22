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
    /*
    这段代码是C++中用于创建一个异步任务处理函数模板的部分，该函数模板接收一个可调用对象F和一组参数Args...，
    并返回一个与异步任务结果关联的std::future对象。以下是详细解释：

    std::future<typename std::result_of<F(Args...)>::type>

    std::future: C++标准库中的组件，用于表示异步计算的结果。
    当你启动一个异步操作时，可以获取一个std::future对象，通过它可以在未来某个时间点查询异步操作是否完成，并获取其返回值。

    typename std::result_of<F(Args...)>::type: 
    这部分是类型推导表达式，使用了C++11引入的std::result_of模板。
    std::result_of可以用来确定给定可调用对象F在传入参数列表Args...时的调用结果类型。
    在这里，它的作用是推断出函数f(args...)调用后的返回类型。

    整体来看，这个返回类型的声明意味着enqueue函数将返回一个std::future对象，
    而这个future所指向的结果数据类型正是由f(args...)调用后得到的类型。

    using return_type = typename std::result_of<F(Args...)>::type;

    using关键字在这里定义了一个别名return_type，它是对上述类型推导结果的引用。

    在后续的代码中，return_type将被用来表示异步任务的返回类型，简化代码并提高可读性。
    */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;
        // 创建共享任务包装器
        /*
        1. std::packaged_task<return_type()>
        std::packaged_task是一个C++标准库中的类模板，它能够封装一个可调用对象（如函数、lambda表达式或函数对象），
        并将该对象的返回值存储在一个可共享的状态中。这里的return_type()表示任务的返回类型是无参数的，
        并且有一个名为return_type的返回类型。

        2. std::make_shared<std::packaged_task<return_type()>>
        std::make_shared是一个工厂函数，用于创建一个std::shared_ptr实例，指向动态分配的对象。
        在这里，它被用来创建一个指向std::packaged_task<return_type()>类型的实例的智能指针。
        用std::make_shared的好处在于它可以一次性分配所有需要的内存（包括管理块和对象本身），从而减少内存分配次数并提高效率，同时确保资源正确释放。
        
        3. std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        - std::bind函数从C++11开始引入，用于将可调用对象与一组给定的参数绑定在一起，
        创建一个新的可调用对象。新的可调用对象可以在以后任意时刻以适当的方式调用原始可调用对象。
            - 在这里，F代表传入的可调用对象类型，而Args代表可能有的参数类型列表。
            - std::forward<F>(f)和std::forward<Args>(args)...是对完美转发的支持，
            使得f和args能以适当的左值引用或右值引用的形式传递给std::bind，
            这样可以保持原表达式的值类别信息，特别是当传递的是右值时，可以避免拷贝开销。
        */
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