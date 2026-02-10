/*==========================================================
 * Server-10: JSON解析 + RESTful API
 *
 * ★ 核心新增功能：
 * 1. JSON格式的请求体解析 (HttpRequest::parseJson)
 * 2. RESTful风格的API设计 (/api/资源/操作)
 * 3. URL查询参数支持 (HttpRequest::getQuery)
 * 4. 改进的路由匹配 (支持带查询参数的URL)
 *
 * ★ 向后兼容：
 * - 保留所有Server-9的form-data接口
 * - 新旧两套接口可以同时使用
 *
 * ★ 实现亮点：
 * - 手写JSON解析器（简化版，仅支持字符串键值对）
 * - 双路由系统（RESTful + 传统）
 * - 统一的JSON响应格式 {"success": bool, "message": string}
 *
 * ★ 技术对比：
 * | 特性       | Server-9        | Server-10          |
 * |------------|----------------|--------------------|
 * | 请求格式   | form-data       | form-data + JSON   |
 * | 响应格式   | HTML            | HTML + JSON        |
 * | 查询参数   | ❌              | ✅                 |
 * | API风格    | 传统            | RESTful            |
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

    /* ========================================
     * ★ 核心改进：双路由系统
     *
     * Server-10 同时支持两套接口：
     * 1. RESTful API (JSON格式) - 新增
     * 2. 传统接口 (Form格式) - 兼容
     * ======================================== */

    // ★ 新增：注册RESTful API路由（JSON格式）
    // 提供 /api/* 前缀的现代化接口
    server.setupRESTfulRoutes();

    // 兼容旧的路由（Form格式）
    // 保留 /register, /login 等传统HTML接口
    server.setupRoutes();

    server.start();
    return 0;
}
