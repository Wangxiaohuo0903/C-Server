#pragma once


#include <sqlite3.h>
#include 
#include 
#include <unordered_map>
#include <openssl/sha.h> // 引入 OpenSSL 的 SHA-256 哈希函数头文件
#include <openssl/evp.h>
#include <openssl/err.h>
#include 
#include 
#include 
#include 


#include "SQLiteConnectionPool.h"


class Database {
private:
std::mutex dbMutex; // 用于同步对数据库访问的互斥锁
std::mutex preparedStatementMutex; // 用于同步预编译 SQL 语句的互斥锁
std::unordered_map<std::string, sqlite3_stmt*> preparedStatements; // 缓存预编译 SQL 语句的 map
SQLiteConnectionPool pool; // 数据库连接池
std::string dbPath; // 数据库文件路径


public:
// 构造函数，用于初始化数据库连接和创建用户表
Database(const std::string& db_path) : dbPath(db_path), pool(5) { // 假设最大连接数为 5
sqlite3* db;
if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
throw std::runtime_error("Failed to open database");
}
// 创建用户表，如果不存在
const char* sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
char* errmsg;
if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
sqlite3_free(errmsg);
sqlite3_close(db);
throw std::runtime_error("Failed to create table: " + std::string(errmsg));
}
sqlite3_close(db);
}


Code
// 析构函数，用于关闭数据库连接（在这个示例中未直接使用）
~Database() {
}

// 生成随机盐值的辅助函数
std::string generateSalt(size_t length) {
    const std::string chars =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<> distribution(0, chars.size() - 1);

    std::string salt;
    for (size_t i = 0; i < length; ++i) {
        salt += chars[distribution(generator)];
    }
    return salt;
}

// 加盐哈希密码的函数
std::string hashPassword(const std::string& password) {
    std::string salt = generateSalt(16); // 生成 16 字节的盐值
    std::string saltedPassword = password + salt; // 将盐值添加到密码末尾

    unsigned char hash[EVP_MAX_MD_SIZE]; // 存储哈希结果
    unsigned int lengthOfHash = 0; // 结果的长度

    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        throw std::runtime_error("Failed to create EVP_MD_CTX");
    }

    if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context, saltedPassword.c_str(), saltedPassword.length()) != 1 ||
        EVP_DigestFinal_ex(context, hash, &lengthOfHash) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("Hashing failed");
    }

    EVP_MD_CTX_free(context);

    std::stringstream ss;
    ss << salt; // 将盐值包含在哈希结果中，以便存储和验证时可以提取盐值
    for (unsigned int i = 0; i < lengthOfHash; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return ss.str();
}

// 获取预编译 SQL 语句的函数
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
            LOG_ERROR("Failed to prepare SQL statement: %s", sql.c_str());
            return nullptr;
        }
    }
}

// 用户注册函数
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
    pool.returnConnection(db); // 完成操作后返回连接到池中
    return true;
}

// 用户登录函数
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

    // 从数据库中获取存储的密码哈希值，其中包含盐值和哈希结果
    const char* stored_password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string dbHashedPwd(stored_password_hash, sqlite3_column_bytes(stmt, 0));
    sqlite3_finalize(stmt);

    // 提取存储的哈希值中的盐值
    std::string salt = dbHashedPwd.substr(0, 16);
    // 使用相同的盐值对用户输入的密码进行哈希
    std::string hashed_input_pwd = hashPasswordWithSalt(password, salt);

    // 比较用户输入的密码哈希值与数据库中存储的哈希值
    if (hashed_input_pwd != dbHashedPwd) {
        LOG_INFO("Login failed for user: %s. Incorrect password.", username.c_str());
        return false;
    }

    LOG_INFO("User logged in: %s", username.c_str());
    return true;
}

};