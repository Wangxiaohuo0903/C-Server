#ifndef DATABASE_H
#define DATABASE_H
#include <sqlite3.h>
#include <string>

class Database {
private:
    sqlite3* db;

public:
    // 构造函数，用于打开数据库并创建用户表
    Database(const std::string& db_path) {
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            throw std::runtime_error("Failed to open database");
        }
        // 创建用户表的SQL语句
        const char* sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
        char* errmsg;
        if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
            throw std::runtime_error("Failed to create table: " + std::string(errmsg));
        }
    }

    // 析构函数，用于关闭数据库连接
    ~Database() {
        sqlite3_close(db);
    }

    // 用户注册函数
    bool registerUser(const std::string& username, const std::string& password) {
        std::string sql = "INSERT INTO users (username, password) VALUES (?, ?);";
        sqlite3_stmt* stmt;

        // 准备SQL语句
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_INFO("Failed to prepare registration SQL for user: " + username); // 记录日志
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

        // 执行SQL语句
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            LOG_INFO("Registration failed for user: " + username); // 记录日志
            sqlite3_finalize(stmt);
            return false;
        }

        // 完成操作，关闭语句
        sqlite3_finalize(stmt);
        LOG_INFO("User registered: " + username + " with password: " + password); // 记录日志
        return true;
    }

    // 用户登录函数
    bool loginUser(const std::string& username, const std::string& password) {
        std::string sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_stmt* stmt;

        // 准备SQL语句
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            LOG_INFO("Failed to prepare login SQL for user: " + username); // 记录日志
            return false;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

        // 执行SQL语句
        if (sqlite3_step(stmt) != SQLITE_ROW) {
            LOG_INFO("User not found: " + username); // 记录日志
            sqlite3_finalize(stmt);
            return false;
        }

        // 获取存储的密码并转换为std::string
        const char* stored_password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string password_str(stored_password, sqlite3_column_bytes(stmt, 0));

        // 检查密码是否匹配
        sqlite3_finalize(stmt);
        if (stored_password == nullptr || password != password_str) {
            LOG_INFO("Login failed for user: " + username + " password:" + password + " stored password is " + password_str); // 记录日志
            return false;
        }

        // 登录成功，记录日志
        LOG_INFO("User logged in: " + username);
        return true;
    }
};
#endif // DATABASE_H