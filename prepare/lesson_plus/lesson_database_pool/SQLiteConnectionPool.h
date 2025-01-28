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

class SQLiteConnectionPool {
public:
    SQLiteConnectionPool(const std::string& dbPath, 
                         size_t minSize = 5,
                         size_t maxSize = 50,
                         int checkIntervalSec = 30)
        : dbPath_(dbPath),
          minSize_(minSize),
          maxSize_(maxSize),
          checkInterval_(checkIntervalSec),
          running_(true) {
        initializePool();
        startMaintenanceThread();
    }

    ~SQLiteConnectionPool() {
        running_ = false;
        if (maintenanceThread_.joinable()) {
            maintenanceThread_.join();
        }
        cleanup();
    }

    class Connection {
    public:
        Connection(sqlite3* conn, SQLiteConnectionPool& pool)
            : conn_(conn), pool_(pool) {
            updateLastUsed();
        }
        ~Connection() {
            if (conn_) {
                pool_.returnConnection(conn_);
            }
        }

        // 为了让你能通过 conn->xxx 调用sqlite3 API
        sqlite3* operator->() const {
            updateLastUsed();
            return conn_;
        }
        sqlite3* get() const {
            updateLastUsed();
            return conn_;
        }
        // 判断连接有效性
        operator bool() const { 
            return conn_ != nullptr; 
        }

    private:
        void updateLastUsed() const {
            auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(pool_.timeMutex_);
            pool_.lastUsedTimes_[conn_] = now;
        }
        sqlite3* conn_;
        SQLiteConnectionPool& pool_;
    };

    // 默认超时 5000ms
    Connection getConnection(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return !pool_.empty() || activeCount_ < maxSize_;
        })) {
            throw std::runtime_error("Get connection timeout");
        }

        if (!pool_.empty()) {
            auto conn = pool_.back();
            pool_.pop_back();
            activeCount_++;
            return Connection(conn, *this);
        }

        if (activeCount_ < maxSize_) {
            auto conn = createNewConnection();
            activeCount_++;
            return Connection(conn, *this);
        }

        throw std::runtime_error("Connection pool exhausted");
    }

private:
    friend class Connection; // Connection需要访问returnConnection

    std::string dbPath_;
    size_t minSize_;
    size_t maxSize_;
    int checkInterval_;
    std::atomic<bool> running_;

    std::vector<sqlite3*> pool_;  // 空闲连接列表
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread maintenanceThread_;
    std::atomic<size_t> activeCount_{0};

    // 追踪每个连接上次使用时间
    std::mutex timeMutex_;
    std::unordered_map<sqlite3*, std::chrono::steady_clock::time_point> lastUsedTimes_;

    void initializePool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < minSize_; ++i) {
            pool_.push_back(createNewConnection());
        }
    }

    sqlite3* createNewConnection() {
        sqlite3* conn = nullptr;
        int rc = sqlite3_open_v2(dbPath_.c_str(), &conn,
                                 SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                                 nullptr);
        if (rc != SQLITE_OK) {
            // 即使失败，也最好先拿errmsg再close
            std::string err = sqlite3_errmsg(conn);
            sqlite3_close(conn);
            throw std::runtime_error("Cannot open database: " + err);
        }
        sqlite3_busy_timeout(conn, 5000);

        {
            std::lock_guard<std::mutex> lock(timeMutex_);
            lastUsedTimes_[conn] = std::chrono::steady_clock::now();
        }
        return conn;
    }

    void returnConnection(sqlite3* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (checkConnectionValid(conn) && pool_.size() < maxSize_) {
            pool_.push_back(conn);
        } else {
            sqlite3_close(conn);
            activeCount_--;
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            lastUsedTimes_.erase(conn);
        }
        cv_.notify_one();
    }

    bool checkConnectionValid(sqlite3* conn) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(conn, "SELECT 1;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    void startMaintenanceThread() {
        maintenanceThread_ = std::thread([this] {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(checkInterval_));
                maintainPool();
            }
        });
    }

    void maintainPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        // 尝试补足到最小连接数
        while (pool_.size() < minSize_ && (activeCount_ + pool_.size() < maxSize_)) {
            pool_.push_back(createNewConnection());
        }

        // 清理长时间不用的空闲连接
        auto now = std::chrono::steady_clock::now();
        auto it = std::remove_if(pool_.begin(), pool_.end(), [&](sqlite3* conn) {
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            auto lastUsed = lastUsedTimes_[conn];
            // 超过30分钟就认为可以回收（仅对空闲连接）
            return (now - lastUsed) > std::chrono::minutes(30);
        });

        // 关闭被标记的连接
        for (auto p = it; p != pool_.end(); ++p) {
            sqlite3_close(*p);
            activeCount_--;
            std::lock_guard<std::mutex> timeLock(timeMutex_);
            lastUsedTimes_.erase(*p);
        }
        pool_.erase(it, pool_.end());
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto conn : pool_) {
            sqlite3_close(conn);
        }
        pool_.clear();
        std::lock_guard<std::mutex> timeLock(timeMutex_);
        lastUsedTimes_.clear();
    }
};
