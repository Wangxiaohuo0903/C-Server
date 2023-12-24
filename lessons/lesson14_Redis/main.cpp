
#include "HttpServer.h"
#include "Database.h"

int main(int argc, char* argv[]) {
    int port = 8080; // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }
    Database db("127.0.0.1:7000,127.0.0.1:7001,127.0.0.1:7002");
    HttpServer server(port, 10, db);
    server.setupRoutes();
    server.start();
    return 0;
}
