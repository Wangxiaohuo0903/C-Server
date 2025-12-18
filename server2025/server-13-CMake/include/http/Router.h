#pragma once
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "database/Database.h"
#include "inference/SimpleInference.h"
#include "inference/SessionManager.h"

/*==========================================================
 * Router类 - Server-12版本
 *
 * ★ 新增功能（相比Server-11）：
 * 1. 会话管理 - SessionManager集成
 * 2. 多轮对话接口 - /chat
 * 3. 上下文拼接策略
 * 4. 会话创建/删除接口
 *
 * 继承功能（来自Server-11）：
 * 1. AI推理接口 - /infer-simple
 * 2. JSON + RESTful API
 * 3. 用户认证系统
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
    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();

        // 先尝试精确匹配
        if (routes.count(key)) {
            return routes[key](request);
        }

        // 处理查询参数的路径（去掉?后面的部分）
        std::string path = request.getPath();
        size_t queryPos = path.find('?');
        if (queryPos != std::string::npos) {
            path = path.substr(0, queryPos);
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

    // ★ Server-12更新：添加SessionManager参数
    void setupRESTfulRoutes(Database& db, SimpleInference& inference, SessionManager& sessionMgr) {

        /* ============================================
         * ★ Server-12新增：多轮对话接口
         * ============================================ */

        /* --- POST /chat/create - 创建新会话 --- */
        addRoute("POST", "/chat/create", [&sessionMgr](const HttpRequest& req) {
            std::string session_id = sessionMgr.createSession();

            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"session_id\":\"" + session_id + "\"}");
            return resp;
        });

        /* --- POST /chat - 多轮对话 --- */
        addRoute("POST", "/chat", [&sessionMgr, &inference](const HttpRequest& req) {
            auto params = req.parseJson();
            std::string session_id = params["session_id"];
            std::string message = params["message"];
            std::string max_tokens_str = params["max_tokens"];

            // 参数验证
            if (session_id.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"session_id required\"}");
                return resp;
            }

            if (message.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"message required\"}");
                return resp;
            }

            // 检查会话是否存在
            if (!sessionMgr.hasSession(session_id)) {
                HttpResponse resp;
                resp.setStatusCode(404);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Session not found\"}");
                return resp;
            }

            // 添加用户消息到历史
            sessionMgr.addUserMessage(session_id, message);

            // 获取最近5轮对话的上下文
            std::string context = sessionMgr.getRecentContext(session_id, 5);

            // 构建完整的prompt（上下文 + 新消息）
            std::string full_prompt = context + "User: " + message + "\nAssistant: ";

            std::cout << "[/chat] Session: " << session_id
                      << ", Context length: " << context.length() << " chars\n";

            // 解析max_tokens
            int max_tokens = 128;  // 默认值增加到128
            if (!max_tokens_str.empty()) {
                try {
                    max_tokens = std::stoi(max_tokens_str);
                } catch (...) {
                    max_tokens = 128;
                }
            }

            // 调用推理引擎生成回复
            std::string response = inference.generate(full_prompt, max_tokens);

            // 添加助手回复到历史
            sessionMgr.addAssistantMessage(session_id, response);

            // 返回响应
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"response\":\"" + response + "\",\"session_id\":\"" + session_id + "\"}");
            return resp;
        });

        /* --- GET /chat/history?session_id=xxx - 获取会话历史 --- */
        addRoute("GET", "/chat/history", [&sessionMgr](const HttpRequest& req) {
            auto params = req.getQuery();
            std::string session_id = params["session_id"];

            if (session_id.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"session_id required\"}");
                return resp;
            }

            if (!sessionMgr.hasSession(session_id)) {
                HttpResponse resp;
                resp.setStatusCode(404);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Session not found\"}");
                return resp;
            }

            std::string context = sessionMgr.getFullContext(session_id);
            int msg_count = sessionMgr.getMessageCount(session_id);

            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"session_id\":\"" + session_id +
                        "\",\"message_count\":" + std::to_string(msg_count) +
                        ",\"history\":\"" + context + "\"}");
            return resp;
        });

        /* --- DELETE /chat/delete - 删除会话 --- */
        addRoute("DELETE", "/chat/delete", [&sessionMgr](const HttpRequest& req) {
            auto params = req.parseJson();
            std::string session_id = params["session_id"];

            if (session_id.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"session_id required\"}");
                return resp;
            }

            if (!sessionMgr.hasSession(session_id)) {
                HttpResponse resp;
                resp.setStatusCode(404);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Session not found\"}");
                return resp;
            }

            sessionMgr.deleteSession(session_id);

            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"message\":\"Session deleted\"}");
            return resp;
        });

        /* ============================================
         * ★ RESTful API 接口（继承自Server-11）
         * ============================================ */

        /* --- POST /infer-simple - ★ Server-11：单轮推理 --- */
        addRoute("POST", "/infer-simple", [&inference](const HttpRequest& req) {
            // 解析JSON请求
            auto params = req.parseJson();
            std::string prompt = params["prompt"];
            std::string max_tokens_str = params["max_tokens"];

            if (prompt.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Prompt required\"}");
                return resp;
            }

            // 解析max_tokens（默认64）
            int max_tokens = 64;
            if (!max_tokens_str.empty()) {
                try {
                    max_tokens = std::stoi(max_tokens_str);
                } catch (...) {
                    max_tokens = 64;
                }
            }

            std::cout << "[/infer-simple] Generating response...\n";

            // 调用推理
            std::string response = inference.generate(prompt, max_tokens);

            std::cout << "[/infer-simple] Response generated: " << response.size() << " bytes\n";

            // 返回JSON响应
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"response\":\"" + response + "\"}");
            return resp;
        });

        /* --- POST /api/users/register - JSON格式注册 --- */
        addRoute("POST", "/api/users/register", [&db](const HttpRequest& req) {
            // 解析JSON请求
            auto params = req.parseJson();
            std::string username = params["username"];
            std::string password = params["password"];

            if (username.empty() || password.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Username and password required\"}");
                return resp;
            }

            // 调用数据库注册
            bool success = db.registerUser(username, password);

            HttpResponse resp;
            resp.setHeader("Content-Type", "application/json");
            if (success) {
                resp.setStatusCode(200);
                resp.setBody("{\"success\":true,\"message\":\"Registration successful\"}");
            } else {
                resp.setStatusCode(400);
                resp.setBody("{\"success\":false,\"message\":\"User already exists\"}");
            }
            return resp;
        });

        /* --- POST /api/users/login - JSON格式登录 --- */
        addRoute("POST", "/api/users/login", [&db](const HttpRequest& req) {
            // 解析JSON请求
            auto params = req.parseJson();
            std::string username = params["username"];
            std::string password = params["password"];

            if (username.empty() || password.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Username and password required\"}");
                return resp;
            }

            // 调用数据库验证
            bool success = db.loginUser(username, password);

            HttpResponse resp;
            resp.setHeader("Content-Type", "application/json");
            if (success) {
                resp.setStatusCode(200);
                resp.setBody("{\"success\":true,\"message\":\"Login successful\",\"username\":\"" + username + "\"}");
            } else {
                resp.setStatusCode(401);
                resp.setBody("{\"success\":false,\"message\":\"Invalid credentials\"}");
            }
            return resp;
        });

        /* --- GET /api/users - 获取用户信息（演示查询参数） --- */
        addRoute("GET", "/api/users", [&db](const HttpRequest& req) {
            auto params = req.getQuery();
            std::string userId = params["id"];

            if (userId.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"User ID required\"}");
                return resp;
            }

            // 简单返回用户ID（实际应该查询数据库）
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"userId\":\"" + userId + "\",\"username\":\"user" + userId + "\"}");
            return resp;
        });

        /* --- POST /api/echo - 测试JSON解析（回显接口） --- */
        addRoute("POST", "/api/echo", [](const HttpRequest& req) {
            auto params = req.parseJson();

            // 构建响应JSON
            std::string responseBody = "{\"success\":true,\"received\":{";
            bool first = true;
            for (const auto& pair : params) {
                if (!first) responseBody += ",";
                responseBody += "\"" + pair.first + "\":\"" + pair.second + "\"";
                first = false;
            }
            responseBody += "}}";

            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody(responseBody);
            return resp;
        });
    }

    // 设置数据库相关的路由（兼容旧版form-data接口）
    void setupDatabaseRoutes(Database& db) {

        /* --- 首页重定向 --- */
        addRoute("GET", "/", [](const HttpRequest&) {
            HttpResponse response;
            response.setStatusCode(302);
            response.setHeader("Location", "/login");
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
