#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
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
            : conn_(conn), pool_(pool), lastUsed_(std::chrono::steady_clock::now()) {}

        ~Connection() {
            if (conn_) {
                pool_.returnConnection(conn_);
            }
        }

        sqlite3* operator->() const { 
            lastUsed_ = std::chrono::steady_clock::now();
            return conn_; 
        }

        operator bool() const { return conn_ != nullptr; }

    private:
        sqlite3* conn_;
        SQLiteConnectionPool& pool_;
        mutable std::chrono::steady_clock::time_point lastUsed_;
    };

    Connection getConnection(int timeoutMs = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到有可用连接或超时
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
    std::string dbPath_;
    size_t minSize_;
    size_t maxSize_;
    int checkInterval_;
    std::atomic<bool> running_;
    
    std::vector<sqlite3*> pool_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread maintenanceThread_;
    std::atomic<size_t> activeCount_{0};

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
            throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(conn)));
        }
        // 设置数据库参数
        sqlite3_busy_timeout(conn, 5000);  // 5秒超时
        return conn;
    }

    void returnConnection(sqlite3* conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (checkConnectionValid(conn) && pool_.size() < maxSize_) {
            pool_.push_back(conn);
        } else {
            sqlite3_close(conn);
            activeCount_--;
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
        // 维护最小连接数
        while (pool_.size() < minSize_ && activeCount_ + pool_.size() < maxSize_) {
            pool_.push_back(createNewConnection());
        }
        
        // 清理过期连接（30分钟未使用）
        auto now = std::chrono::steady_clock::now();
        auto it = std::remove_if(pool_.begin(), pool_.end(), [&](sqlite3* conn) {
            auto lastUsed = std::chrono::time_point_cast<std::chrono::minutes>(
                static_cast<Connection*>(sqlite3_get_auxdata(conn, 0))->lastUsed_
            );
            return (now - lastUsed) > std::chrono::minutes(30);
        });
        
        for (auto p = it; p != pool_.end(); ++p) {
            sqlite3_close(*p);
        }
        pool_.erase(it, pool_.end());
    }

    void cleanup() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto conn : pool_) {
            sqlite3_close(conn);
        }
        pool_.clear();
    }
};