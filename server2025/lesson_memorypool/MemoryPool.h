#pragma once

// MemoryPool 类模板用于对象的池化管理，通过复用已分配的对象，减少内存分配和释放的开销
template <typename T>
class MemoryPool {
public:
    // 构造函数，初始化内存池
    // initial_size: 初始池中对象的数量，默认为 100
    // max_pool_size: 池中允许的最大对象数量，默认为 1000
    MemoryPool(size_t initial_size = 100, size_t max_pool_size = 1000)
        : max_size_(max_pool_size),    // 设置最大池大小
          allocated_(0) {               // 已分配对象计数初始化为 0
        // 根据 initial_size 预先创建 initial_size 个对象，并将它们放入池中
        for (size_t i = 0; i < initial_size; ++i) {
            // 创建一个共享指针，带有自定义删除器，当对象释放时将其返回池中
            pool_.push_back(std::shared_ptr<T>(new T(), [this](T* ptr) {
                this->release(ptr);  // 当对象不再使用时，将其返回到内存池
            }));
        }
    }

    // 获取一个对象
    // 如果池中有对象，则从池中取出并返回；如果池为空且总分配数小于最大池大小，则创建新的对象
    std::shared_ptr<T> acquire() {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁，确保线程安全
        // 如果池中有可用对象，则返回池中的最后一个对象
        if (!pool_.empty()) {
            auto obj = pool_.back();  // 获取池中的最后一个对象
            pool_.pop_back();         // 从池中移除该对象
            return obj;               // 返回该对象
        }

        // 如果池为空，且当前已分配的对象数小于最大池大小，创建一个新对象
        if (allocated_.fetch_add(1, std::memory_order_relaxed) < max_size_) {
            // 创建一个新的对象，并返回一个共享指针，带有自定义删除器
            auto new_obj = std::shared_ptr<T>(new T(), [this](T* ptr) { this->release(ptr); });
            return new_obj;
        }

        // 如果池已满，返回空指针
        return nullptr;  
    }

    // 将对象归还到池中
    // 如果池未满，将对象放回池中；如果池已满，则删除对象并减少已分配对象计数
    void release(T* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁，确保线程安全
        // 如果池中空间未满，添加对象到池中
        if (pool_.size() < max_size_) {
            pool_.push_back(std::shared_ptr<T>(ptr, [this](T* p) {
                this->release(p);  // 当对象不再使用时，将其归还池中
            }));
        } else {
            // 如果池已满，删除该对象并减少已分配对象计数
            delete ptr;
            allocated_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

private:
    std::vector<std::shared_ptr<T>> pool_;  // 用于存储池中的对象，采用共享指针管理对象生命周期
    std::mutex mutex_;                     // 互斥锁，确保多线程环境下对池的访问是线程安全的
    const size_t max_size_;                // 池中允许的最大对象数量
    std::atomic<size_t> allocated_;        // 已分配对象的数量，原子操作以确保并发环境下的安全
};
