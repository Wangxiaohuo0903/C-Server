#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <unordered_map>
#include <sqlite3.h>

// 定义SQLite数据库连接池类
class SQLiteConnectionPool {
public:
    // 构造函数：初始化连接池的参数
    // dbPath: 数据库文件路径
    // minSize: 最小连接数，默认为5
    // maxSize: 最大连接数，默认为50
    // checkIntervalSec: 连接池维护线程检查空闲连接的时间间隔，单位秒，默认为30秒
    SQLiteConnectionPool(const std::string& dbPath, 
                         size_t minSize = 5,
                         size_t maxSize = 50,
                         int checkIntervalSec = 30)
        : dbPath_(dbPath), // 数据库路径
          minSize_(minSize), // 最小连接数
          maxSize_(maxSize), // 最大连接数
          checkInterval_(checkIntervalSec), // 维护线程检查间隔
          running_(true) {  // 线程池运行状态，默认为true
        initializePool(); // 初始化连接池
        startMaintenanceThread(); // 启动维护线程
    }

    // 析构函数：停止线程池并清理资源
    ~SQLiteConnectionPool() {
        running_ = false; // 设置停止标志，通知维护线程退出
        if (maintenanceThread_.joinable()) {
            maintenanceThread_.join(); // 等待维护线程结束
        }
        cleanup(); // 清理资源
    }

    // 内部Connection类，用于表示连接池中的连接
    class Connection {
    public:
        // 构造函数：初始化连接并更新最后使用时间
        Connection(sqlite3* conn, SQLiteConnectionPool& pool)
            : conn_(conn), pool_(pool) {
            updateLastUsed(); // 更新连接的最后使用时间
        }

        // 析构函数：连接对象销毁时，将连接返回连接池
        ~Connection() {
            if (conn_) {
                pool_.returnConnection(conn_); // 连接返回池中
            }
        }

        // 重载->运算符，使得可以通过conn->xxx调用SQLite的API
        sqlite3* operator->() const {
            updateLastUsed(); // 每次访问连接时，更新其最后使用时间
            return conn_;
        }

        // 返回原始连接
        sqlite3* get() const {
            updateLastUsed(); // 每次访问连接时，更新其最后使用时间
            return conn_;
        }

        // 判断连接是否有效
        operator bool() const { 
            return conn_ != nullptr; // 如果连接为空指针，则返回false
        }

    private:
        // 更新连接的最后使用时间
        void updateLastUsed() const {
            auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(pool_.timeMutex_);
            pool_.lastUsedTimes_[conn_] = now; // 更新连接的最后使用时间
        }
        
        sqlite3* conn_;  // SQLite连接对象
        SQLiteConnectionPool& pool_;  // 连接池引用
    };

    // 获取连接：超时时间为timeoutMs，默认5000ms
    Connection getConnection(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待直到连接池中有可用连接或可以创建新的连接
        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return !pool_.empty() || activeCount_ < maxSize_;
        })) {
            throw std::runtime_error("Get connection timeout"); // 超时错误
        }

        // 从池中获取一个连接
        if (!pool_.empty()) {
            auto conn = pool_.back();
            pool_.pop_back(); // 从池中移除该连接
            activeCount_++; // 增加活跃连接计数
            return Connection(conn, *this);
        }

        // 如果没有可用连接且活跃连接数小于最大连接数，创建新连接
        if (activeCount_ < maxSize_) {
            auto conn = createNewConnection();
            activeCount_++;
            return Connection(conn, *this);
        }

        throw std::runtime_error("Connection pool exhausted"); // 如果池已满，抛出错误
    }

private:
    friend class Connection; // Connection类需要访问returnConnection方法

    std::string dbPath_; // 数据库路径
    size_t minSize_; // 最小连接数
    size_t maxSize_; // 最大连接数
    int checkInterval_; // 检查间隔（秒）
    std::atomic<bool> running_; // 连接池是否正在运行

    std::vector<sqlite3*> pool_;  // 存储空闲连接的池
    std::mutex mutex_; // 保护连接池的互斥锁
    std::condition_variable cv_; // 条件变量，控制连接获取的同步
    std::thread maintenanceThread_; // 维护线程，用于定期清理连接池
    std::atomic<size_t> activeCount_{0}; // 当前活跃的连接数量

    // 追踪每个连接的最后使用时间
    std::mutex timeMutex_; 
    std::unordered_map<sqlite3*, std::chrono::steady_clock::time_point> lastUsedTimes_;

    // 初始化连接池：创建最小连接数的连接并放入池中
    void initializePool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < minSize_; ++i) {
            pool_.push_back(createNewConnection()); // 创建新连接并添加到池中
        }
    }

    // 创建新的SQLite连接
    sqlite3* createNewConnection() {
        sqlite3* conn = nullptr;
        // 使用sqlite3_open_v2创建新的数据库连接
        int rc = sqlite3_open_v2(dbPath_.c_str(), &conn,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            // 如果连接失败，打印错误并抛出异常
            std::string err = sqlite3_errmsg(conn);
            sqlite3_close(conn);
            throw std::runtime_error("Cannot open database: " + err);
        }
        sqlite3_busy_timeout(conn, 5000); // 设置SQLite忙碌超时

        {
            std::lock_guard<std::mutex> lock(timeMutex_);
            lastUsedTimes_[conn] = std::chrono::steady_clock::now(); // 记录连接的最后使用时间
        }
        return conn;
    }

    // 将连接返回连接池
    void returnConnection(sqlite3* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (checkConnectionValid(conn) && pool_.size() < maxSize_) {
            pool_.push_back(conn); // 如果连接有效并且池未满，返回连接到池中
        } else {
            sqlite3_close(conn); // 否则关闭连接
            activeCount_--; // 活跃连接数减少
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            lastUsedTimes_.erase(conn); // 清除该连接的最后使用时间记录
        }
        cv_.notify_one(); // 唤醒一个等待中的线程
    }

    // 检查连接是否有效
    bool checkConnectionValid(sqlite3* conn) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(conn, "SELECT 1;", nullptr, nullptr, &errMsg); // 执行简单查询以验证连接
        if (rc != SQLITE_OK) {
            sqlite3_free(errMsg);
            return false; // 如果连接无效，返回false
        }
        return true;
    }

    // 启动维护线程：定期检查连接池并进行维护
    void startMaintenanceThread() {
        maintenanceThread_ = std::thread([this] {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(checkInterval_)); // 等待一段时间
                maintainPool(); // 执行连接池维护操作
            }
        });
    }

    // 维护连接池：清理过期连接并保持池中连接数满足最小连接数
    void maintainPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 如果池中连接数少于最小值，尝试补充连接
        while (pool_.size() < minSize_ && (activeCount_ + pool_.size() < maxSize_)) {
            pool_.push_back(createNewConnection());
        }

        // 清理长时间未使用的连接
        auto now = std::chrono::steady_clock::now();
        auto it = std::remove_if(pool_.begin(), pool_.end(), [&](sqlite3* conn) {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            auto lastUsed = lastUsedTimes_[conn];
            // 超过30分钟未使用的连接被认为可以回收
            return (now - lastUsed) > std::chrono::minutes(30);
        });

        // 关闭被标记为过期的连接
        for (auto p = it; p != pool_.end(); ++p) {
            sqlite3_close(*p);
            activeCount_--;
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            lastUsedTimes_.erase(*p);
        }
        pool_.erase(it, pool_.end()); // 从池中移除过期连接
    }

    // 清理连接池中的所有连接
    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto conn : pool_) {
            sqlite3_close(conn); // 关闭所有连接
        }
        pool_.clear(); // 清空连接池
        std::lock_guard<std::mutex> timeLock(timeMutex_);
        lastUsedTimes_.clear(); // 清空最后使用时间记录
    }
};
