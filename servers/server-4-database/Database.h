#include <sqlite3.h>
#include <string>

class Database {
private:
    sqlite3* db;

public:
    // 构造函数，用于打开数据库并创建用户表
Database(const std::string& db_path) {
    // 使用sqlite3_open打开数据库
    // db_path.c_str()将C++字符串转换为C风格字符串，因为sqlite3_open需要C风格字符串
    // &db是指向数据库连接对象的指针
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        // 如果数据库打开失败，抛出运行时错误
        throw std::runtime_error("Failed to open database");
    }

    // 定义创建用户表的SQL语句
    // "CREATE TABLE IF NOT EXISTS"语句用于创建一个新表，如果表已存在则不重复创建
    // "users"是表名
    // "username"和"password"是表的列名，分别用来存储用户名和密码
    // "username TEXT PRIMARY KEY"定义了username为文本类型的主键
    // "password TEXT"定义了password为文本类型
    const char* sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
    char* errmsg;

    // 执行SQL语句创建表
    // sqlite3_exec用于执行SQL语句
    // db是数据库连接对象
    // sql是待执行的SQL语句
    // 后面的参数是回调函数和它的参数，这里不需要回调所以传0
    // &errmsg用于存储错误信息
    if (sqlite3_exec(db, sql, 0, 0, &errmsg) != SQLITE_OK) {
        // 如果创建表失败，抛出运行时错误，并附带错误信息
        throw std::runtime_error("Failed to create table: " + std::string(errmsg));
    }
}

// 析构函数，用于关闭数据库连接
~Database() {
    // 关闭数据库连接
    // sqlite3_close用于关闭和释放数据库连接资源
    // db是数据库连接对象
    sqlite3_close(db);
}


    // 用户注册函数
bool registerUser(const std::string& username, const std::string& password) {
    // 构建SQL语句用于插入新用户
    std::string sql = "INSERT INTO users (username, password) VALUES (?, ?);";
    sqlite3_stmt* stmt;

    // 准备SQL语句，预编译以防止SQL注入攻击
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        // 如果准备SQL语句失败，记录日志并返回false
        LOG_INFO("Failed to prepare registration SQL for user: %s" , username.c_str());
        return false;
    }

    // 绑定SQL语句中的参数，防止SQL注入
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    // 执行SQL语句，完成用户注册
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        // 如果执行失败，记录日志，清理资源并返回false
        LOG_INFO("Registration failed for user: %s" , username.c_str());
        sqlite3_finalize(stmt);
        return false;
    }

    // 清理资源，关闭SQL语句
    sqlite3_finalize(stmt);
    // 记录用户注册成功的日志，实际上不建议这么写日志，不安全，这里为了方便看效果
    LOG_INFO("User registered: %s with password: %s" ,username.c_str() ,password.c_str());
    return true;
}

// 用户登录函数
bool loginUser(const std::string& username, const std::string& password) {
    // 构建查询用户密码的SQL语句
    std::string sql = "SELECT password FROM users WHERE username = ?;";
    sqlite3_stmt* stmt;

    // 准备SQL语句
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        // 如果准备SQL语句失败，记录日志并返回false
        LOG_INFO("Failed to prepare login SQL for user: %s" , username.c_str());
        return false;
    }

    // 绑定用户名参数
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    // 执行SQL语句
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        // 如果用户名不存在，记录日志，清理资源并返回false
        LOG_INFO("User not found: %s" , username.c_str());
        sqlite3_finalize(stmt);
        return false;
    }

    // 获取查询结果中的密码
    const char* stored_password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string password_str(stored_password, sqlite3_column_bytes(stmt, 0));

    // 检查密码是否匹配
    sqlite3_finalize(stmt);
    if (stored_password == nullptr || password != password_str) {
        // 如果密码不匹配，记录日志并返回false
        LOG_INFO("Login failed for user: %s password: %s, stored password is %s" ,username.c_str(), password.c_str(), password_str.c_str());
        return false;
    }

    // 登录成功，记录日志
    LOG_INFO("User logged in: %s" , username.c_str());
    return true;
}

};
