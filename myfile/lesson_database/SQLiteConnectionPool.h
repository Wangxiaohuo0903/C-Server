#include <vector>
#include <memory>
#include <mutex>

class SQLiteConnectionPool {
private:
    std::vector<sqlite3*> pool;
    std::mutex poolMutex;
    size_t maxPoolSize;

public:
    SQLiteConnectionPool(size_t maxSize) : maxPoolSize(maxSize) {}

    ~SQLiteConnectionPool() {
        std::lock_guard<std::mutex> guard(poolMutex);
        for (auto conn : pool) {
            sqlite3_close(conn);
        }
    }

    sqlite3* getConnection(const std::string& dbPath) {
        std::lock_guard<std::mutex> guard(poolMutex);
        if (!pool.empty()) {
            sqlite3* conn = pool.back();
            pool.pop_back();
            return conn;
        }

        sqlite3* newConn;
        if (sqlite3_open(dbPath.c_str(), &newConn) != SQLITE_OK) {
            throw std::runtime_error("Failed to open database");
        }
        return newConn;
    }

    void returnConnection(sqlite3* conn) {
        std::lock_guard<std::mutex> guard(poolMutex);
        if (pool.size() < maxPoolSize) {
            pool.push_back(conn);
        } else {
            sqlite3_close(conn);
        }
    }
};
