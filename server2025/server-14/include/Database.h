#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <sstream>

class Database {
private:
    sqlite3* db;
    std::mutex dbMutex; // 互斥锁，用于同步对数据库的访问

    // 内部建表
    void initTables() {
        const char* user_sql =
            "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
        const char* chat_sql =
            "CREATE TABLE IF NOT EXISTS chats ("
            "chat_id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "username TEXT, "
            "title TEXT DEFAULT '', "
            "session_id TEXT DEFAULT '', "
            "created DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        const char* msg_sql =
            "CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "chat_id INTEGER, "
            "role TEXT, "
            "content TEXT, "
            "ts DATETIME DEFAULT CURRENT_TIMESTAMP"
            ");";
        char* errmsg;
        if (sqlite3_exec(db, user_sql, 0, 0, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Failed to create user table: " + std::string(errmsg));
        if (sqlite3_exec(db, chat_sql, 0, 0, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Failed to create chat table: " + std::string(errmsg));
        if (sqlite3_exec(db, msg_sql, 0, 0, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Failed to create msg table: " + std::string(errmsg));

        // Server-14: 数据库迁移 - 如果 session_id 列不存在则添加
        const char* check_col_sql = "SELECT session_id FROM chats LIMIT 1;";
        bool column_exists = (sqlite3_exec(db, check_col_sql, 0, 0, &errmsg) == SQLITE_OK);

        if (!column_exists) {
            // 列不存在，添加它
            const char* alter_sql = "ALTER TABLE chats ADD COLUMN session_id TEXT DEFAULT '';";
            if (sqlite3_exec(db, alter_sql, 0, 0, &errmsg) != SQLITE_OK)
                throw std::runtime_error("Failed to add session_id column: " + std::string(errmsg));
        }

        // 添加索引以提高查询性能
        const char* idx_sql = "CREATE INDEX IF NOT EXISTS idx_session_id ON chats(session_id);";
        if (sqlite3_exec(db, idx_sql, 0, 0, &errmsg) != SQLITE_OK)
            throw std::runtime_error("Failed to create index: " + std::string(errmsg));
    }

public:
    // 构造与析构
    Database(const std::string& db_path) {
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
            throw std::runtime_error("Failed to open database");
        initTables();
    }
    ~Database() { sqlite3_close(db); }

    // 用户注册
    bool registerUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);
        std::string sql = "INSERT INTO users (username, password) VALUES (?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    // 用户登录
    bool loginUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex);
        std::string sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        bool found = sqlite3_step(stmt) == SQLITE_ROW;
        if (!found) { sqlite3_finalize(stmt); return false; }
        const char* stored_password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        std::string password_str(stored_password ? stored_password : "",
                                 stored_password ? sqlite3_column_bytes(stmt, 0) : 0);
        sqlite3_finalize(stmt);
        return stored_password && password == password_str;
    }

    // 新建 chat，返回 chat_id
    int createChat(const std::string& username) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "INSERT INTO chats(username) VALUES(?);";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return -1;
        sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); return -1; }
        int id = (int)sqlite3_last_insert_rowid(db);
        sqlite3_finalize(st);
        return id;
    }

    // 插入一条消息
    bool addMessage(int chatId, const std::string& role, const std::string& content) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "INSERT INTO messages(chat_id, role, content) VALUES (?, ?, ?);";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(st, 1, chatId);
        sqlite3_bind_text(st, 2, role.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(st, 3, content.c_str(), -1, SQLITE_STATIC);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        return ok;
    }

    // 返回用户的所有 chat_id（降序）
    std::vector<int> listChats(const std::string& username) {
        std::vector<int> v;
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "SELECT chat_id FROM chats WHERE username=? ORDER BY created DESC;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return v;
        sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(st) == SQLITE_ROW)
            v.push_back(sqlite3_column_int(st, 0));
        sqlite3_finalize(st);
        return v;
    }

    // 返回单个 chat 下所有消息 [role, content]
    std::vector<std::pair<std::string, std::string>> getMessages(int chatId) {
        std::vector<std::pair<std::string, std::string>> v;
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "SELECT role, content FROM messages WHERE chat_id=? ORDER BY id ASC;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return v;
        sqlite3_bind_int(st, 1, chatId);
        while (sqlite3_step(st) == SQLITE_ROW) {
            const char* role = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            v.emplace_back(role ? role : "", content ? content : "");
        }
        sqlite3_finalize(st);
        return v;
    }

    // ========== Server-14 新增方法：Session 支持 ==========

    // 链接 session_id 到 chat_id
    bool linkSessionToChat(const std::string& session_id, int chat_id) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "UPDATE chats SET session_id=? WHERE chat_id=?;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(st, 1, session_id.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 2, chat_id);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        return ok;
    }

    // 获取 session 对应的 chat_id
    int getChatIdForSession(const std::string& session_id) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "SELECT chat_id FROM chats WHERE session_id=?;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return -1;
        sqlite3_bind_text(st, 1, session_id.c_str(), -1, SQLITE_STATIC);
        int chat_id = -1;
        if (sqlite3_step(st) == SQLITE_ROW) {
            chat_id = sqlite3_column_int(st, 0);
        }
        sqlite3_finalize(st);
        return chat_id;
    }

    // Chat 信息结构体
    struct ChatInfo {
        int chat_id;
        std::string session_id;
        std::string username;
        std::string title;
        std::string created;
    };

    // 加载用户的所有 chats
    std::vector<ChatInfo> loadUserChats(const std::string& username) {
        std::vector<ChatInfo> chats;
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "SELECT chat_id, session_id, username, title, created FROM chats WHERE username=? ORDER BY created DESC;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return chats;
        sqlite3_bind_text(st, 1, username.c_str(), -1, SQLITE_STATIC);

        while (sqlite3_step(st) == SQLITE_ROW) {
            ChatInfo info;
            info.chat_id = sqlite3_column_int(st, 0);

            const char* sid = reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
            info.session_id = sid ? sid : "";

            const char* uname = reinterpret_cast<const char*>(sqlite3_column_text(st, 2));
            info.username = uname ? uname : "";

            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(st, 3));
            info.title = t ? t : "";

            const char* c = reinterpret_cast<const char*>(sqlite3_column_text(st, 4));
            info.created = c ? c : "";

            chats.push_back(info);
        }
        sqlite3_finalize(st);
        return chats;
    }

    // 更新 chat 标题
    bool updateChatTitle(int chat_id, const std::string& new_title) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "UPDATE chats SET title=? WHERE chat_id=?;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_text(st, 1, new_title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(st, 2, chat_id);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        return ok;
    }

    // 删除 chat 及其所有消息
    bool deleteChat(int chat_id) {
        std::lock_guard<std::mutex> g(dbMutex);

        // 先删除消息
        const char* del_msg_sql = "DELETE FROM messages WHERE chat_id=?;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, del_msg_sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(st, 1, chat_id);
        sqlite3_step(st);
        sqlite3_finalize(st);

        // 再删除 chat
        const char* del_chat_sql = "DELETE FROM chats WHERE chat_id=?;";
        if (sqlite3_prepare_v2(db, del_chat_sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(st, 1, chat_id);
        bool ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_finalize(st);
        return ok;
    }

    // 验证 chat 所有权
    bool verifyChatOwnership(int chat_id, const std::string& username) {
        std::lock_guard<std::mutex> g(dbMutex);
        const char* sql = "SELECT username FROM chats WHERE chat_id=?;";
        sqlite3_stmt* st;
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK)
            return false;
        sqlite3_bind_int(st, 1, chat_id);

        bool is_owner = false;
        if (sqlite3_step(st) == SQLITE_ROW) {
            const char* owner = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
            if (owner && username == owner) {
                is_owner = true;
            }
        }
        sqlite3_finalize(st);
        return is_owner;
    }
};
