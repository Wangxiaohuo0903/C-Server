#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"

// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router {
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

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

    // 设置数据库相关的路由，例如注册和登录
    void setupDatabaseRoutes(Database& db) {
        // 注册路由
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();  // 解析表单数据
            std::string username = params["username"];
            std::string password = params["password"];
            // 调用数据库方法进行用户注册
            if (db.registerUser(username, password)) {
                return HttpResponse::makeOkResponse("Register Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        // 登录路由
        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();  // 解析表单数据
            std::string username = params["username"];
            std::string password = params["password"];
            // 调用数据库方法进行用户登录
            if (db.loginUser(username, password)) {
                return HttpResponse::makeOkResponse("Login Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Login Failed!");
            }
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 存储路由映射
};
