#include "DatabaseManager.h"
#include "log.h"
#include <iostream>

DatabaseManager::DatabaseManager(const std::string& db_path) : db(nullptr), database_path(db_path) {
    LOG_INFO("DatabaseManager created with database path: " + db_path);
}

DatabaseManager::~DatabaseManager() {
    if (db) {
        sqlite3_close(db);
        LOG_INFO("Database closed");
    }
}

bool DatabaseManager::open() {
    if (sqlite3_open(database_path.c_str(), &db) != SQLITE_OK) {
        LOG_ERROR("Error opening database: " + std::string(sqlite3_errmsg(db)));
        return false;
    }
    LOG_INFO("Database opened successfully");
    return true;
}

bool DatabaseManager::createUser(const std::string& username, const std::string& password) {
    std::string sql = "INSERT INTO users (username, password) VALUES ('" + username + "', '" + password + "');";
    char* errorMessage = nullptr;
    int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errorMessage);

    if (result != SQLITE_OK) {
        LOG_ERROR("SQL error in createUser: " + std::string(errorMessage));
        sqlite3_free(errorMessage);
        return false;
    }

    LOG_INFO("User created: " + username);
    return true;
}

bool DatabaseManager::validateUser(const std::string& username, const std::string& password) {
    std::string sql = "SELECT * FROM users WHERE username='" + username + "' AND password='" + password + "';";
    char* errorMessage = nullptr;
    bool valid = false;
    int result = sqlite3_exec(db, sql.c_str(), [](void* valid, int count, char** data, char** columns) -> int {
        *(static_cast<bool*>(valid)) = count > 0;
        return 0;
    }, &valid, &errorMessage);

    if (result != SQLITE_OK) {
        LOG_ERROR("SQL error in validateUser: " + std::string(errorMessage));
        sqlite3_free(errorMessage);
        return false;
    }

    LOG_INFO("User validation " + std::string(valid ? "successful" : "failed") + " for username: " + username);
    return valid;
}
