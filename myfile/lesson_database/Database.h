#pragma once
#include <sqlite3.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <openssl/sha.h> // 引入openssl的SHA-256哈希函数头文件
#include <openssl/evp.h>
#include <openssl/err.h>
#include <stdexcept>
#include <iomanip>
#include <sstream>

#include "SQLiteConnectionPool.h" 
class Database {
private:
    
    std::mutex dbMutex; // 互斥锁，用于同步对数据库的访问
    std::mutex preparedStatementMutex;
       // 新增用于缓存预编译语句的map
    std::unordered_map<std::string, sqlite3_stmt*> preparedStatements;
    SQLiteConnectionPool pool; // 数据库连接池
    std::string dbPath;

public:
    // 构造函数，用于打开数据库并创建用户表
    Database(const std::string& db_path) : dbPath(db_path), pool(5) { // 假设最大连接数为5
        // 初始化数据库结构，这里可以使用一个单独的连接
        sqlite3* db;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Failed to open database");
        }
        const char* sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
        char* errmsg;
        if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
            sqlite3_free(errmsg);
            sqlite3_close(db); // 确保在抛出异常前关闭数据库连接
            throw std::runtime_error("Failed to create table: " + std::string(errmsg));
        }
        sqlite3_close(db); // 关闭初始化时使用的连接
    }

    // 析构函数，用于关闭数据库连接
    ~Database() {
        // sqlite3_close(db);
    }

    // 新增用于SHA-256哈希密码的函数
    std::string hashPassword(const std::string& password) {
        unsigned char hash[EVP_MAX_MD_SIZE]; // 存储哈希结果
        unsigned int lengthOfHash = 0; // 结果的长度

        EVP_MD_CTX* context = EVP_MD_CTX_new();
        if(context == nullptr) {
            throw std::runtime_error("Failed to create EVP_MD_CTX");
        }

        if(EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("Failed to initialize digest context");
        }

        if(EVP_DigestUpdate(context, password.c_str(), password.length()) != 1) {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("Failed to update digest");
        }

        if(EVP_DigestFinal_ex(context, hash, &lengthOfHash) != 1) {
            EVP_MD_CTX_free(context);
            throw std::runtime_error("Failed to finalize digest");
        }

        EVP_MD_CTX_free(context);

        std::stringstream ss;
        for(unsigned int i = 0; i < lengthOfHash; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }

        return ss.str();
    }

    sqlite3_stmt* getPreparedStatement(sqlite3* db, const std::string& sql) {
        std::lock_guard<std::mutex> guard(preparedStatementMutex);

        auto it = preparedStatements.find(sql);
        if (it != preparedStatements.end()) {
            return it->second;
        } else {
            sqlite3_stmt* newStmt;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &newStmt, nullptr) == SQLITE_OK) {
                preparedStatements[sql] = newStmt;
                return newStmt;
            } else {
                // 注意处理错误情况
                LOG_ERROR("Failed to prepare SQL statement: %s", sql.c_str());
                return nullptr;
            }
        }
    }
  bool registerUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);
        sqlite3* db = pool.getConnection(dbPath); // 从连接池获取连接
        sqlite3_stmt* stmt = getPreparedStatement(db, "INSERT INTO users (username, password) VALUES (?, ?);");
        if (!stmt) {
            LOG_INFO("Failed to prepare or retrieve registration SQL for user: %s", username.c_str());
            return false;
        }

        std::string hashed_password = hashPassword(password); // 对密码进行哈希处理

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hashed_password.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_INFO("Registration failed for user: %s", username.c_str());
            sqlite3_reset(stmt);
            return false;
        }

        sqlite3_reset(stmt);
        LOG_INFO("User registered: %s with hashed password: %s", username.c_str(), hashed_password.c_str());
        pool.returnConnection(db); // 操作完成后返回连接到池中
        return true;
    }

    bool loginUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);
        sqlite3* db = pool.getConnection(dbPath); // 从连接池获取连接
        std::string sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_INFO("Failed to prepare login SQL for user: %s", username.c_str());
            return false;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            LOG_INFO("User not found: %s", username.c_str());
            sqlite3_finalize(stmt);
            return false;
        }

        const char* stored_password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string password_str(stored_password_hash, sqlite3_column_bytes(stmt, 0));

        sqlite3_finalize(stmt);

        std::string hashed_password = hashPassword(password); // 对输入的密码进行哈希处理
        if (hashed_password != password_str) {
            LOG_INFO("Login failed for user: %s. Incorrect password.", username.c_str());
            return false;
        }

        LOG_INFO("User logged in: %s", username.c_str());
        pool.returnConnection(db); // 操作完成后返回连接到池中
        return true;
    }
    
};
