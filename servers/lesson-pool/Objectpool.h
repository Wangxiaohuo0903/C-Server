#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

// 模板类 ObjectPool，用于管理对象的复用
template <typename T>
class ObjectPool {
public:
    // 构造函数，初始化内存池
    // initial_size: 初始池中对象的数量
    // max_pool_size: 池中允许的最大对象数量
    ObjectPool(size_t initial_size = 100, size_t max_pool_size = 1000)
        : max_size_(max_pool_size),
          allocated_(0) { // 初始化已分配对象计数为0
        // 预先创建 initial_size 个对象，并将它们加入池中
        for (size_t i = 0; i < initial_size; ++i) {
            // 创建一个共享指针，带有自定义删除器，用于将对象返还池中
            pool_.push_back(std::shared_ptr<T>(new T(), [this](T* ptr) {
                this->release(ptr);
            }));
        }
    }

    // 获取一个对象
    std::shared_ptr<T> acquire() {
        // 尝试无锁获取一个对象
        if (!pool_.empty()) {
            std::lock_guard<std::mutex> lock(mutex_); // 加锁以确保线程安全
            if (!pool_.empty()) { // 再次检查池是否为空
                auto obj = pool_.back(); // 获取池中的最后一个对象
                pool_.pop_back(); // 从池中移除该对象
                return obj; // 返回该对象
            }
        }

        // 如果池中没有可用对象，创建一个新对象
        std::shared_ptr<T> new_obj(
            new T(),
            [this](T* ptr) { this->release(ptr); } // 自定义删除器，用于将对象返还池中
        );

        // 控制最大池大小
        // 原子操作，增加已分配对象计数
        if (allocated_.fetch_add(1, std::memory_order_relaxed) >= max_size_) {
            allocated_.fetch_sub(1, std::memory_order_relaxed); // 超过最大池大小，减少计数
            return std::shared_ptr<T>(new T()); // 返回一个普通的共享指针，不带自定义删除器
        }

        return new_obj; // 返回新创建的对象
    }

    // 获取当前池中对象的数量
    size_t size() const { 
        std::lock_guard<std::mutex> lock(mutex_); // 加锁以确保线程安全
        return pool_.size(); 
    }

private:
    // 释放对象，将其返还给池中
    void release(T* ptr) {
        // 重置对象状态，确保下次使用时是干净的
        ptr->reset();

        std::lock_guard<std::mutex> lock(mutex_); // 加锁以确保线程安全
        if (pool_.size() < max_size_) { // 如果池中还有空间
            // 将对象放回池中，带有自定义删除器
            pool_.push_back(std::shared_ptr<T>(ptr, [this](T* p) {
                this->release(p);
            }));
        } else {
            // 如果池已满，删除对象并减少已分配计数
            delete ptr;
            allocated_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    std::vector<std::shared_ptr<T>> pool_; // 存储对象的池
    mutable std::mutex mutex_; // 保护池的互斥锁，mutable 允许在 const 方法中锁定
    const size_t max_size_; // 池中允许的最大对象数量
    std::atomic<size_t> allocated_; // 已分配对象的数量，使用原子操作以支持并发
};
