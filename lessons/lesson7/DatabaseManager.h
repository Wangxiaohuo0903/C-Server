#include "log.h"
#include <sqlite3.h>
#include <string>

class DatabaseManager {
public:
    DatabaseManager(const std::string& db_path);
    ~DatabaseManager();

    bool open();
    bool createUser(const std::string& username, const std::string& password);
    bool validateUser(const std::string& username, const std::string& password);

private:
    sqlite3* db;
    std::string database_path;
};
