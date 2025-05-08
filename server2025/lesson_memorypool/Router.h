#pragma once
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "MemoryPool.h"  // 引入内存池

// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router {
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    // 构造函数：初始化内存池
    Router() {
        requestPool = std::make_shared<MemoryPool<HttpRequest>>(100);  // 预分配100个 HttpRequest 对象
        responsePool = std::make_shared<MemoryPool<HttpResponse>>(100);  // 预分配100个 HttpResponse 对象
    }

    // 添加路由：将 HTTP 方法和路径映射到处理函数
    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    // 根据 HTTP 请求路由到相应的处理函数
    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();
        if (routes.count(key)) {
            return routes[key](request);
        }
        // 如果没有找到匹配的路由，返回 404 Not Found 响应
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    // 读取文件内容
    std::string readFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return "Error: Unable to open file " + filePath;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // 设置数据库相关的路由，例如注册和登录
    void setupDatabaseRoutes(Database& db) {

        addRoute("GET", "/login", [this](const HttpRequest& req) {
            auto request = requestPool->acquire();  // 从内存池中获取 HttpRequest 对象
            auto response = responsePool->acquire();  // 从内存池中获取 HttpResponse 对象

            // 使用内存池获取的对象进行处理
            response->setStatusCode(200);
            response->setHeader("Content-Type", "text/html");
            response->setBody(readFile("UI/login.html"));
            request->reset();  // 重置请求对象
            return *response;  // 返回响应
        });

        addRoute("GET", "/register", [this](const HttpRequest& req) {
            auto request = requestPool->acquire();
            auto response = responsePool->acquire();

            response->setStatusCode(200);
            response->setHeader("Content-Type", "text/html");
            response->setBody(readFile("UI/register.html"));
            request->reset();
            return *response;
        });

        // 注册路由
        addRoute("POST", "/register", [&db, this](const HttpRequest& req) {
            auto request = requestPool->acquire();  // 获取请求对象
            auto response = responsePool->acquire();  // 获取响应对象

            // 解析表单数据
            auto params = request->parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];

            // 调用数据库方法进行用户注册
            if (db.registerUser(username, password)) {
                response->setStatusCode(200);
                response->setHeader("Content-Type", "text/html");
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
                response->setBody(responseBody);
            } else {
                *response = HttpResponse::makeErrorResponse(400, "Register Failed!");
            }

            request->reset();  // 重置请求对象
            return *response;  // 返回响应
        });

        // 登录路由
        addRoute("POST", "/login", [&db, this](const HttpRequest& req) {
            auto request = requestPool->acquire();  // 获取请求对象
            auto response = responsePool->acquire();  // 获取响应对象

            // 解析表单数据
            auto params = request->parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];

            // 调用数据库方法进行用户登录
            if (db.loginUser(username, password)) {
                response->setStatusCode(200);
                response->setHeader("Content-Type", "text/html");
                response->setBody("<html><body><h2>Login Successful</h2></body></html>");
            } else {
                *response = HttpResponse::makeErrorResponse(401, "Login Failed");
            }

            request->reset();  // 重置请求对象
            return *response;  // 返回响应
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 存储路由映射
    std::shared_ptr<MemoryPool<HttpRequest>> requestPool;  // 管理 HttpRequest 对象的内存池
    std::shared_ptr<MemoryPool<HttpResponse>> responsePool;  // 管理 HttpResponse 对象的内存池
};
