#pragma once
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"

/*==========================================================
 * Router类 - Server-10版本
 *
 * ★ 新增功能：
 * 1. RESTful风格的API设计
 * 2. JSON格式的请求和响应
 * 3. 查询参数解析支持
 *=========================================================*/

// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router {
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    // 添加路由：将 HTTP 方法和路径映射到处理函数
    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    // ★ 改进：根据 HTTP 请求路由到相应的处理函数（支持查询参数）
    //
    // Server-9: 只支持精确匹配 (GET|/api/users 必须完全一致)
    // Server-10: 支持查询参数 (GET|/api/users?id=123 也能匹配到 GET|/api/users)
    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();

        /* ========================================
         * 步骤1: 先尝试精确匹配
         * 例如: GET /login → 查找 "GET|/login"
         * ======================================== */
        if (routes.count(key)) {
            return routes[key](request);
        }

        /* ========================================
         * 步骤2: 处理带查询参数的路径（去掉?后面的部分）
         * 例如: GET /api/users?id=123
         *       → 去掉?后变成 GET /api/users
         *       → 查找 "GET|/api/users"
         * 这样就可以用同一个路由处理函数处理不同的查询参数
         * ======================================== */
        std::string path = request.getPath();
        size_t queryPos = path.find('?');
        if (queryPos != std::string::npos) {
            path = path.substr(0, queryPos);  // 截取?前面的路径部分
            key = request.getMethodString() + "|" + path;
            if (routes.count(key)) {
                return routes[key](request);
            }
        }

        // 如果没有找到匹配的路由，返回 404 Not Found 响应
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }


    std::string readFile(const std::string& filePath) {
        // 使用标准库中的ifstream打开文件
        std::ifstream file(filePath);

        // 判断文件是否成功打开
        if (!file.is_open()) {
            // 若未能成功打开文件，返回错误信息
            return "Error: Unable to open file " + filePath;
        }

        // 使用stringstream来读取文件内容
        std::stringstream buffer;
        // 将文件内容读入到stringstream中
        buffer << file.rdbuf();

        // 将读取的内容转换为字符串并返回
        return buffer.str();
    }

    // ★ 新增：设置RESTful API路由（JSON格式）
    //
    // 这是Server-10的核心新增功能！
    // 提供了现代化的RESTful API接口，使用JSON格式进行数据交互
    //
    // 与传统form-data格式的区别：
    // - 请求: Content-Type: application/json
    // - 响应: Content-Type: application/json
    // - 易于JavaScript前端调用（fetch API）
    // - 数据结构更清晰，支持嵌套对象（虽然这个简化版暂不支持）
    void setupRESTfulRoutes(Database& db) {

        /* ============================================
         * ★ RESTful API 接口（使用JSON格式）
         *
         * API设计风格：
         * - 使用 /api/* 前缀区分传统页面路由
         * - 使用 /api/资源名/操作 的URL结构
         * - 返回统一的JSON格式: {"success": bool, "message": string, ...}
         * ============================================ */

        /* ==================================================================
         * POST /api/users/register - JSON格式注册
         *
         * 请求示例:
         *   POST /api/users/register
         *   Content-Type: application/json
         *   {"username":"alice","password":"123456"}
         *
         * 成功响应:
         *   HTTP/1.1 200 OK
         *   Content-Type: application/json
         *   {"success":true,"message":"Registration successful"}
         *
         * 失败响应:
         *   HTTP/1.1 400 Bad Request
         *   Content-Type: application/json
         *   {"success":false,"message":"User already exists"}
         * ================================================================== */
        addRoute("POST", "/api/users/register", [&db](const HttpRequest& req) {
            // 使用parseJson()解析请求体（Server-10新增功能）
            auto params = req.parseJson();
            std::string username = params["username"];
            std::string password = params["password"];

            // 参数验证
            if (username.empty() || password.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);  // 400 Bad Request
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Username and password required\"}");
                return resp;
            }

            // 调用数据库注册（与Server-9相同的逻辑）
            bool success = db.registerUser(username, password);

            // 构建JSON响应（注意：手动拼接JSON字符串）
            HttpResponse resp;
            resp.setHeader("Content-Type", "application/json");
            if (success) {
                resp.setStatusCode(200);  // 200 OK
                resp.setBody("{\"success\":true,\"message\":\"Registration successful\"}");
            } else {
                resp.setStatusCode(400);  // 400 Bad Request
                resp.setBody("{\"success\":false,\"message\":\"User already exists\"}");
            }
            return resp;
        });

        /* ==================================================================
         * POST /api/users/login - JSON格式登录
         *
         * 请求示例:
         *   POST /api/users/login
         *   Content-Type: application/json
         *   {"username":"alice","password":"123456"}
         *
         * 成功响应:
         *   HTTP/1.1 200 OK
         *   Content-Type: application/json
         *   {"success":true,"message":"Login successful","username":"alice"}
         *
         * 失败响应:
         *   HTTP/1.1 401 Unauthorized
         *   Content-Type: application/json
         *   {"success":false,"message":"Invalid credentials"}
         * ================================================================== */
        addRoute("POST", "/api/users/login", [&db](const HttpRequest& req) {
            // 解析JSON请求体
            auto params = req.parseJson();
            std::string username = params["username"];
            std::string password = params["password"];

            // 参数验证
            if (username.empty() || password.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Username and password required\"}");
                return resp;
            }

            // 调用数据库验证（与Server-9相同的逻辑）
            bool success = db.loginUser(username, password);

            // 构建JSON响应
            HttpResponse resp;
            resp.setHeader("Content-Type", "application/json");
            if (success) {
                resp.setStatusCode(200);  // 200 OK
                // 注意：在响应中包含用户名，前端可以用来显示欢迎信息
                resp.setBody("{\"success\":true,\"message\":\"Login successful\",\"username\":\"" + username + "\"}");
            } else {
                resp.setStatusCode(401);  // 401 Unauthorized（未授权）
                resp.setBody("{\"success\":false,\"message\":\"Invalid credentials\"}");
            }
            return resp;
        });

        /* ==================================================================
         * GET /api/users - 获取用户信息（演示查询参数）
         *
         * ★ 这个接口演示如何处理URL查询参数（Server-10新增功能）
         *
         * 请求示例:
         *   GET /api/users?id=123
         *
         * 成功响应:
         *   HTTP/1.1 200 OK
         *   Content-Type: application/json
         *   {"success":true,"userId":"123","username":"user123"}
         *
         * 失败响应:
         *   HTTP/1.1 400 Bad Request
         *   Content-Type: application/json
         *   {"success":false,"message":"User ID required"}
         *
         * 注意：
         * - GET请求通常使用查询参数传递数据（因为GET没有body）
         * - POST请求通常使用body传递数据（form-data或JSON）
         * ================================================================== */
        addRoute("GET", "/api/users", [&db](const HttpRequest& req) {
            // 使用getQuery()解析查询参数（Server-10新增功能）
            auto params = req.getQuery();  // /api/users?id=123 → {{"id", "123"}}
            std::string userId = params["id"];

            // 参数验证
            if (userId.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"User ID required\"}");
                return resp;
            }

            // 简单返回用户ID（实际应该查询数据库获取完整用户信息）
            // 这里为了演示，直接构造假数据
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"userId\":\"" + userId + "\",\"username\":\"user" + userId + "\"}");
            return resp;
        });

        /* ==================================================================
         * POST /api/echo - 测试JSON解析（回显接口）
         *
         * ★ 这是一个测试接口，用于验证JSON解析功能是否正常工作
         * 会将接收到的JSON数据原样返回，方便调试
         *
         * 请求示例:
         *   POST /api/echo
         *   Content-Type: application/json
         *   {"name":"alice","age":"25","city":"beijing"}
         *
         * 响应:
         *   HTTP/1.1 200 OK
         *   Content-Type: application/json
         *   {"success":true,"received":{"name":"alice","age":"25","city":"beijing"}}
         *
         * 用途：
         * - 测试JSON解析器是否工作正常
         * - 调试前端发送的JSON数据格式
         * - 学习如何手动构建嵌套的JSON响应
         * ================================================================== */
        addRoute("POST", "/api/echo", [](const HttpRequest& req) {
            // 解析JSON请求体
            auto params = req.parseJson();

            /* ========================================
             * 手动构建嵌套的JSON响应
             * 目标格式: {"success":true,"received":{...}}
             * ======================================== */
            std::string responseBody = "{\"success\":true,\"received\":{";
            bool first = true;
            for (const auto& pair : params) {
                if (!first) responseBody += ",";  // 键值对之间加逗号
                responseBody += "\"" + pair.first + "\":\"" + pair.second + "\"";
                first = false;
            }
            responseBody += "}}";  // 闭合received对象和外层对象

            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody(responseBody);
            return resp;
        });
    }

    /* ============================================
     * 设置数据库相关的路由（兼容旧版form-data接口）
     *
     * ★ 这些是Server-9时代的传统接口，Server-10保留以确保向后兼容
     *
     * 特点：
     * - 使用form-data格式（application/x-www-form-urlencoded）
     * - 返回HTML页面（而非JSON）
     * - 适合浏览器直接访问
     *
     * 对比新增的RESTful接口：
     * - RESTful: JSON请求/响应，适合前后端分离
     * - 传统: Form请求，HTML响应，适合传统Web应用
     * ============================================ */
    void setupDatabaseRoutes(Database& db) {

        /* --- 首页重定向 --- */
        addRoute("GET", "/", [](const HttpRequest&) {
            HttpResponse response;
            response.setStatusCode(302);  // 302 Found（临时重定向）
            response.setHeader("Location", "/login");  // 重定向到登录页
            return response;
        });

        addRoute("GET", "/login", [this](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/login.html"));
            return response;
        });

        addRoute("GET", "/register", [this](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/register.html"));
            return response;
        });

        // 注册路由（form-data格式）
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];

            if (db.registerUser(username, password)) {
                HttpResponse response;
                response.setStatusCode(200);
                response.setHeader("Content-Type", "text/html");
                std::string responseBody = R"(
                    <html>
                    <head>
                        <title>Register Success</title>
                        <script type="text/javascript">
                            alert("Register Success!");
                            window.location = "/login";
                        </script>
                    </head>
                    <body>
                        <h2>moving to login...</h2>
                    </body>
                    </html>
                )";
                response.setBody(responseBody);
                return response;
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        // 登录路由（form-data格式）
        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];
            LOG_INFO("Start check user info");

            if (db.loginUser(username, password)) {
                HttpResponse response;
                response.setStatusCode(200);
                response.setHeader("Content-Type", "text/html");
                response.setBody("<html><body><h2>Login Successful</h2></body></html>");
                LOG_INFO("Login Success!");
                return response;
            } else {
                HttpResponse response;
                response.setStatusCode(401);
                response.setHeader("Content-Type", "text/html");
                response.setBody("<html><body><h2>Login Failed</h2></body></html>");
                return response;
            }
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 存储路由映射
};
