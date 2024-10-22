#pragma once
#include <sqlite3.h> // 引入sqlite3数据库的头文件，用于操作SQLite数据库
#include <string>
#include <mutex> // 用于线程同步
#include <unordered_map> // 用于存储预编译SQL语句
#include <openssl/sha.h> // 引入OpenSSL的SHA-256哈希函数头文件
#include <openssl/evp.h>
#include <openssl/err.h>
#include <stdexcept> // 用于异常处理
#include <iomanip> // 用于格式化输出
#include <sstream> // 用于字符串流操作

#include "SQLiteConnectionPool.h" // 引入自定义的SQLite数据库连接池类头文件
class Database {
private:
    std::mutex dbMutex; // 用于同步对数据库的访问，保证线程安全
    std::mutex preparedStatementMutex; // 用于同步预编译语句的创建和访问
    std::unordered_map<std::string, sqlite3_stmt*> preparedStatements; // 用于缓存预编译SQL语句，提高效率
    SQLiteConnectionPool pool; // 数据库连接池，管理数据库连接
    std::string dbPath; // 数据库文件的路径

public:
    // 构造函数，初始化数据库连接池并创建用户表
    Database(const std::string& db_path) : dbPath(db_path), pool(5) { // 假设连接池的最大连接数为5
        sqlite3* db;
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Failed to open database");
        }
        // 创建用户表，如果表已存在则忽略
        const char* sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
        char* errmsg;
        if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
            sqlite3_free(errmsg);
            sqlite3_close(db); // 确保在抛出异常前关闭数据库连接
            throw std::runtime_error("Failed to create table: " + std::string(errmsg));
        }
        sqlite3_close(db); // 初始化完成后关闭数据库连接
    }

    // 析构函数，资源清理在SQLiteConnectionPool中自动完成，这里不需要手动关闭数据库连接

    // 使用SHA-256算法哈希密码
    std::string hashPassword(const std::string& password) {
        unsigned char hash[EVP_MAX_MD_SIZE]; // 存储哈希结果的数组，EVP_MAX_MD_SIZE是OpenSSL为哈希结果预定义的最大字节长度
        unsigned int lengthOfHash = 0; // 实际哈希结果的长度

        EVP_MD_CTX* context = EVP_MD_CTX_new(); // 创建一个新的哈希上下文，用于执行哈希运算
        if(context == nullptr) {
            throw std::runtime_error("Failed to create EVP_MD_CTX"); // 如果创建哈希上下文失败，则抛出异常
        }

        if(EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) { // 初始化哈希上下文为SHA-256算法
            EVP_MD_CTX_free(context); // 如果初始化失败，释放之前创建的哈希上下文
            throw std::runtime_error("Failed to initialize digest context"); // 并抛出异常
        }

        if(EVP_DigestUpdate(context, password.c_str(), password.length()) != 1) { // 将密码添加到哈希上下文中进行哈希运算
            EVP_MD_CTX_free(context); // 如果添加失败，释放哈希上下文
            throw std::runtime_error("Failed to update digest"); // 并抛出异常
        }

        if(EVP_DigestFinal_ex(context, hash, &lengthOfHash) != 1) { // 完成哈希计算，并将结果存储在hash数组中，lengthOfHash变量会被更新为哈希结果的实际长度
            EVP_MD_CTX_free(context); // 如果完成哈希计算失败，释放哈希上下文
            throw std::runtime_error("Failed to finalize digest"); // 并抛出异常
        }

        EVP_MD_CTX_free(context); // 哈希计算完成后，释放哈希上下文

        // 将哈希结果转换为十六进制字符串
        std::stringstream ss;
        for(unsigned int i = 0; i < lengthOfHash; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i]; // 遍历哈希结果的每个字节，将其转换为十六进制表示，并添加到字符串流中
        }

        return ss.str(); // 返回哈希结果的十六进制字符串表示
    }


    // 从缓存或新建中获取预编译SQL语句
    sqlite3_stmt* getPreparedStatement(sqlite3* db, const std::string& sql) {
        std::lock_guard<std::mutex> guard(preparedStatementMutex); // 加锁保护预编译语句的map

        // 查找是否已有预编译的语句
        auto it = preparedStatements.find(sql);
        if (it != preparedStatements.end()) {
            return it->second; // 如果找到，直接返回
        } else {
            // 否则，预编译新的SQL语句
            sqlite3_stmt* newStmt;
            if (sqlite3_prepare_v2(db, sql.c_str(), -1, &newStmt, nullptr) == SQLITE_OK) {
                preparedStatements[sql] = newStmt; // 缓存新的预编译语句
                return newStmt; // 返回新预编译的语句
            } else {
                // 预编译失败时的处理逻辑
                LOG_ERROR("Failed to prepare SQL statement: %s", sql.c_str());
                return nullptr;
            }
        }
    }

    // 用户注册功能实现
    bool registerUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex); // 加锁保护数据库操作
        sqlite3* db = pool.getConnection(dbPath); // 从连接池中获取一个连接
        sqlite3_stmt* stmt = getPreparedStatement(db, "INSERT INTO users (username, password) VALUES (?, ?);");
        if (!stmt) {
            LOG_INFO("Failed to prepare or retrieve registration SQL for user: %s", username.c_str());
            return false;
        }

        // 对密码进行哈希处理并绑定参数
        std::string hashed_password = hashPassword(password); 

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, hashed_password.c_str(), -1, SQLITE_STATIC);

        // 执行SQL语句并检查结果
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_INFO("Registration failed for user: %s", username.c_str());
            sqlite3_reset(stmt); // 重置语句状态以便下次使用
            return false;
        }

        sqlite3_reset(stmt); // 重置语句状态以便下次使用
        LOG_INFO("User registered: %s with hashed password: %s", username.c_str(), hashed_password.c_str());
        pool.returnConnection(db); // 完成操作后返回连接到池中
        return true;
    }

    // 用户登录功能实现
    bool loginUser(const std::string& username, const std::string& password) {
        sqlite3* db = pool.getConnection(dbPath); // 从连接池中获取一个连接
        std::string sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_INFO("Failed to prepare login SQL for user: %s", username.c_str());
            return false;
        }

        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        // 执行查询并检查结果
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            LOG_INFO("User not found: %s", username.c_str());
            sqlite3_finalize(stmt); // 释放语句资源
            return false;
        }

        const char* stored_password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string password_str(stored_password_hash, sqlite3_column_bytes(stmt, 0));

        sqlite3_finalize(stmt); // 释放语句资源

        std::string hashed_password = hashPassword(password); // 对输入的密码进行哈希处理
        if (hashed_password != password_str) {
            LOG_INFO("Login failed for user: %s. Incorrect password.", username.c_str());
            return false;
        }

        LOG_INFO("User logged in: %s", username.c_str());
        pool.returnConnection(db); // 完成操作后返回连接到池中
        return true;
    }
};

