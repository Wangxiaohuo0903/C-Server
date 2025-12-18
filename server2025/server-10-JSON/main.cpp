/*==========================================================
 * Server-10: JSON解析 + RESTful API
 *
 * 新增功能：
 * 1. JSON格式的请求体解析
 * 2. RESTful风格的API设计
 * 3. 查询参数支持
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include <iostream>

int main(int argc, char* argv[]) {
    int port = 8080; // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }

    std::cout << "=== Server-10: JSON + RESTful API ===\n";
    std::cout << "Port: " << port << "\n";
    std::cout << "\n新增API接口:\n";
    std::cout << "  POST /api/users/register - JSON格式注册\n";
    std::cout << "  POST /api/users/login    - JSON格式登录\n";
    std::cout << "  GET  /api/users?id=xxx   - 获取用户信息\n";
    std::cout << "  POST /api/echo           - JSON回显测试\n";
    std::cout << "\n传统接口（兼容）:\n";
    std::cout << "  POST /register           - Form格式注册\n";
    std::cout << "  POST /login              - Form格式登录\n";
    std::cout << "======================================\n\n";

    Database db("users.db"); // 初始化数据库
    HttpServer server(port, 10, db);

    // ★ 新增：注册RESTful API路由
    server.setupRESTfulRoutes();

    // 兼容旧的路由
    server.setupRoutes();

    server.start();
    return 0;
}
