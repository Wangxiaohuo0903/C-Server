#pragma once
#include <sqlite3.h>
#include <string>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <openssl/rand.h>  // 用于生成随机盐值

#include <openssl/sha.h>  // 如果需要hashPassword
// #include <openssl/evp.h>
// #include <openssl/err.h>

#include "SQLiteConnectionPool.h"

class Database {
private:
    std::mutex dbMutex;                // 保护数据库操作的互斥锁
    std::mutex preparedStatementMutex; // 保护preparedStatements的互斥锁
    std::unordered_map<std::string, sqlite3_stmt*> preparedStatements;

    SQLiteConnectionPool pool;
    std::string dbPath;
        // 生成一个随机盐值
    std::string generateSalt(size_t length = 16) {
        unsigned char salt[length];
        if (!RAND_bytes(salt, length)) {
            throw std::runtime_error("Failed to generate salt");
        }

        std::ostringstream oss;
        oss << std::hex << std::setw(2) << std::setfill('0');
        for (int i = 0; i < length; ++i) {
            oss << (int)salt[i];
        }

        return oss.str();
    }

    // 哈希密码和盐值
    std::string hashPasswordWithSalt(const std::string& password, const std::string& salt) {
        std::string saltedPassword = password + salt;
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(saltedPassword.data()), 
               saltedPassword.size(), 
               hash);

        std::ostringstream oss;
        oss << std::hex << std::setw(2) << std::setfill('0');
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            oss << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return oss.str();
    }

public:
    Database(const std::string& db_path) 
        : dbPath(db_path),
          // 假设最小连接数 5，最大连接数 20
          pool(db_path, 5, 20) 
    {
        // 初始化数据库表
        auto conn = pool.getConnection();  // RAII句柄
        sqlite3* db = conn.get();

        const char* sql = 
            "CREATE TABLE IF NOT EXISTS users ("
            "    username TEXT PRIMARY KEY, "
            "    password TEXT, "
            "    salt TEXT"
            ");";


        char* errmsg = nullptr;
        int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string errCopy = errmsg ? errmsg : "unknown error";
            sqlite3_free(errmsg);
            throw std::runtime_error("Failed to create table: " + errCopy);
        }
    }

    // 简单的SHA256 hash函数
    std::string hashPassword(const std::string& password) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(password.data()), 
               password.size(), 
               hash);

        std::ostringstream oss;
        oss << std::hex << std::setw(2) << std::setfill('0');
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            oss << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return oss.str();
    }

    // 获取或者创建一个预编译stmt
    sqlite3_stmt* getPreparedStatement(sqlite3* db, const std::string& sql) {
        std::lock_guard<std::mutex> statementLock(preparedStatementMutex);

        auto it = preparedStatements.find(sql);
        if (it != preparedStatements.end()) {
            // 已经准备好过，直接返回
            return it->second;
        }
        // 还没准备过，创建一个新的
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return nullptr;
        }
        preparedStatements[sql] = stmt;
        return stmt;
    }

     bool registerUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);

        // 从连接池获取连接（默认超时5000ms）
        auto conn = pool.getConnection();  
        sqlite3* db = conn.get();

        sqlite3_stmt* stmt = getPreparedStatement(db, 
            "INSERT INTO users (username, password, salt) VALUES (?, ?, ?);");
        if (!stmt) {
            return false;
        }

        // 生成盐值
        std::string salt = generateSalt();

        // 使用盐值对密码进行哈希加盐
        std::string hashed_password = hashPasswordWithSalt(password, salt);

        // 绑定参数
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hashed_password.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_reset(stmt);
            return false;
        }

        sqlite3_reset(stmt);
        return true; 
    }

    // 修改loginUser来支持盐值
    bool loginUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);

        auto conn = pool.getConnection();
        sqlite3* db = conn.get();

        std::string sql = "SELECT password, salt FROM users WHERE username = ?;";
        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            return false;
        }

        const unsigned char* stored_pass = sqlite3_column_text(stmt, 0);
        const unsigned char* stored_salt = sqlite3_column_text(stmt, 1);
        std::string dbStoredHash = stored_pass ? reinterpret_cast<const char*>(stored_pass) : "";
        std::string dbStoredSalt = stored_salt ? reinterpret_cast<const char*>(stored_salt) : "";

        sqlite3_finalize(stmt);

        // 使用盐值和密码进行哈希比较
        std::string hashed_password = hashPasswordWithSalt(password, dbStoredSalt);
        return (hashed_password == dbStoredHash);
    }
};
