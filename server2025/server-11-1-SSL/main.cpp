
#include "HttpServer.h"
#include "Database.h"

int main() {
    Database db("users.db"); // 初始化数据库
    HttpServer server(8080, 10, db);
    server.setupRoutes();
    server.start();
    return 0;
}
