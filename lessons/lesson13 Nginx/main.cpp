
#include "HttpServer.h"
#include "Database.h"

int main(int argc, char* argv[]) {
    Database db("users.db"); // 初始化数据库
    // lesson 13新增
    int port = 8080; // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }
    HttpServer server(port, 10, db);
    server.setupRoutes();
    server.start();
    return 0;
}
