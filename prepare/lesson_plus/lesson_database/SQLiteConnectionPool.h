和#include <vector>
#include <mutex>
#include <chrono>
#include <sqlite3.h>
#include <iostream> // 用于简单的日志记录
#include <algorithm>

class SQLiteConnectionPool {
private:
    // 定义存储数据库连接和最后使用时间的结构体
    struct ConnectionInfo {
        sqlite3* connection; // SQLite数据库连接
        std::chrono::steady_clock::time_point lastUsed; // 记录连接最后一次使用的时间点
    };

    std::vector<ConnectionInfo> pool; // 数据库连接池
    std::mutex poolMutex; // 互斥锁，保护数据库连接池的并发访问
    size_t maxPoolSize; // 连接池的最大容量
    const std::chrono::seconds connectionTimeout = std::chrono::seconds(300); // 定义连接超时时间，这里设置为300秒

    // 清理超时未使用的连接
    void cleanup() {
        auto now = std::chrono::steady_clock::now();
        //遍历连接池中的每个连接，检查每个连接自上次使用以来是否已超过设定的超时时间（300秒）。如果某个连接已超时，就关闭这个连接，并从连接池中移除该连接
        pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const ConnectionInfo& info) {
            // 计算连接的空闲时间
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - info.lastUsed);
            if (duration > connectionTimeout) {
                // 如果超时，则关闭该连接并从池中移除
                sqlite3_close(info.connection);
                return true;
            }
            return false;
        }), pool.end());
    }

    // 检查数据库连接是否仍然有效
    bool isConnectionValid(sqlite3* conn) {
        // 通过执行一个简单的查询（SELECT 1）来测试连接是否有效
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(conn, "SELECT 1", -1, &stmt, nullptr) != SQLITE_OK) {
            // 如果准备语句失败，则认为连接无效
            return false;
        }
        bool valid = sqlite3_step(stmt) == SQLITE_ROW; // 执行查询，检查是否成功获取结果
        sqlite3_finalize(stmt); // 清理查询语句
        return valid;
    }

public:
    // 构造函数，初始化连接池最大容量
    SQLiteConnectionPool(size_t maxSize) : maxPoolSize(maxSize) {}

    // 析构函数，关闭并清理所有数据库连接
    ~SQLiteConnectionPool() {
        std::lock_guard<std::mutex> guard(poolMutex); // 加锁
        for (auto& info : pool) {
            sqlite3_close(info.connection); // 关闭每个连接
        }
    }

    // 从连接池中获取一个数据库连接
    sqlite3* getConnection(const std::string& dbPath) {
        std::lock_guard<std::mutex> guard(poolMutex); // 加锁
        cleanup(); // 获取连接前先执行一次清理操作

        // 遍历连接池
        for (auto it = pool.begin(); it != pool.end();) {
            if (isConnectionValid(it->connection)) {
                // 如果找到一个有效的连接，则将其从池中移除并返回
                sqlite3* conn = it->connection;
                pool.erase(it);
                return conn;
            } else {
                // 如果连接无效，则关闭并从池中移除
                sqlite3_close(it->connection);
                it = pool.erase(it);
            }
        }

        // 如果没有可用的空闲连接，则创建一个新的连接
        sqlite3* newConn;
        if (sqlite3_open(dbPath.c_str(), &newConn) != SQLITE_OK) {
            std::cerr << "Failed to open database: " << dbPath << std::endl;
            throw std::runtime_error("Failed to open database");
        }
        return newConn;
    }

    // 将不再需要的数据库连接返回给连接池
    void returnConnection(sqlite3* conn) {
        std::lock_guard<std::mutex> guard(poolMutex); // 加锁
        if (pool.size() < maxPoolSize && isConnectionValid(conn)) {
            // 如果连接池未满且连接有效，将连接返回到池中
            pool.push_back({conn, std::chrono::steady_clock::now()});
        } else {
            // 如果连接池已满或连接无效，关闭连接
            sqlite3_close(conn);
        }
    }
};
