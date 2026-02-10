#pragma once
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "SimpleInference.h" // ★ Server-11新增

/*==========================================================
 * Router类 - Server-11版本
 *
 * ★ 新增功能（相比Server-10）：
 * 1. AI推理接口 - /infer-simple
 * 2. SimpleInference集成
 * 3. 单轮文本生成
 *
 * 继承功能（来自Server-10）：
 * 1. RESTful风格的API设计
 * 2. JSON格式的请求和响应
 * 3. 查询参数解析支持
 *=========================================================*/

// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router
{
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest &)>;

    // 添加路由：将 HTTP 方法和路径映射到处理函数
    void addRoute(const std::string &method, const std::string &path, HandlerFunc handler)
    {
        routes[method + "|" + path] = handler;
    }

    // ★ 改进：根据 HTTP 请求路由到相应的处理函数（支持查询参数）
    HttpResponse routeRequest(const HttpRequest &request)
    {
        std::string key = request.getMethodString() + "|" + request.getPath();

        // 先尝试精确匹配
        if (routes.count(key))
        {
            return routes[key](request);
        }

        // 处理查询参数的路径（去掉?后面的部分）
        std::string path = request.getPath();
        size_t queryPos = path.find('?');
        if (queryPos != std::string::npos)
        {
            path = path.substr(0, queryPos);
            key = request.getMethodString() + "|" + path;
            if (routes.count(key))
            {
                return routes[key](request);
            }
        }

        // 如果没有找到匹配的路由，返回 404 Not Found 响应
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    std::string readFile(const std::string &filePath)
    {
        // 使用标准库中的ifstream打开文件
        std::ifstream file(filePath);

        // 判断文件是否成功打开
        if (!file.is_open())
        {
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

    /**
     * @brief 设置RESTful API路由
     *
     * ★ Server-11新增参数：
     * - inference: SimpleInference引用，用于AI推理
     *
     * Server-10 vs Server-11：
     * - Server-10: setupRESTfulRoutes(Database& db)
     * - Server-11: setupRESTfulRoutes(Database& db, SimpleInference& inference)
     */
    void setupRESTfulRoutes(Database &db, SimpleInference &inference)
    {

        /* ============================================
         * ★ RESTful API 接口（使用JSON格式）
         * ============================================ */

        /* ============================================================================
         * POST /infer-simple - ★ Server-11新增：LLM单轮推理接口
         *
         * 请求格式：
         *   POST /infer-simple
         *   Content-Type: application/json
         *   {"prompt":"你好，介绍一下你自己", "max_tokens":"64"}
         *
         * 响应格式：
         *   {"success":true, "response":"您好！我是一个AI助手..."}
         *
         * curl测试：
         *   curl -X POST http://localhost:8080/infer-simple \
         *     -H 'Content-Type: application/json' \
         *     -d '{"prompt":"你好", "max_tokens":"64"}'
         * ============================================================================ */
        addRoute("POST", "/infer-simple", [&inference](const HttpRequest &req)
                 {
            // 步骤1: 解析JSON请求体
            auto params = req.parseJson();
            std::string prompt = params["prompt"];              // 用户提示
            std::string max_tokens_str = params["max_tokens"];  // 生成长度

            // 步骤2: 验证必需参数
            if (prompt.empty()) {
                HttpResponse resp;
                resp.setStatusCode(400);  // 400 Bad Request
                resp.setHeader("Content-Type", "application/json");
                resp.setBody("{\"success\":false,\"message\":\"Prompt required\"}");
                return resp;
            }

            // 步骤3: 解析max_tokens（可选参数，默认64）
            int max_tokens = 64;
            if (!max_tokens_str.empty()) {
                try {
                    max_tokens = std::stoi(max_tokens_str);
                } catch (...) {
                    max_tokens = 64;  // 转换失败使用默认值
                }
            }

            std::cout << "[/infer-simple] Generating response...\n";

            // 步骤4: 调用LLM推理 ★核心
            // 内部流程：Tokenize -> Transformer -> 自回归生成 -> Detokenize
            std::string response = inference.generate(prompt, max_tokens);

            std::cout << "[/infer-simple] Response generated: " << response.size() << " bytes\n";

            // 步骤5: 构建JSON响应
            HttpResponse resp;
            resp.setStatusCode(200);  // 200 OK
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":true,\"response\":\"" + response + "\"}");
            return resp; });
    }

    // 设置数据库相关的路由（兼容旧版form-data接口）
    void setupDatabaseRoutes(Database &db)
    {

        /* --- 首页重定向 --- */
        addRoute("GET", "/", [](const HttpRequest &)
                 {
            HttpResponse response;
            response.setStatusCode(302);
            response.setHeader("Location", "/login");
            return response; });

        addRoute("GET", "/login", [this](const HttpRequest &req)
                 {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/login.html"));
            return response; });

        addRoute("GET", "/register", [this](const HttpRequest &req)
                 {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/register.html"));
            return response; });

        // 注册路由（form-data格式）
        addRoute("POST", "/register", [&db](const HttpRequest &req)
                 {
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
            } });

        // 登录路由（form-data格式）
        addRoute("POST", "/login", [&db](const HttpRequest &req)
                 {
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
            } });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes; // 存储路由映射
};
