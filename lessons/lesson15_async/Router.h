#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include <functional>
#include <unordered_map>
#include <future>

class Router {
public:
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();
        if (routes.count(key)) {
            return routes[key](request);
        }
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    void setupDatabaseRoutes(Database& db) {
         // 注册路由
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody(); 
            std::string username = params["username"];
            std::string password = params["password"];
            // 异步调用数据库注册方法
            auto future = db.registerUserAsync(username, password);
            future.wait(); // 等待异步操作完成
            if (future.get()) {
                return HttpResponse::makeOkResponse("Register Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        // 登录路由
        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];
            // 异步调用数据库登录方法
            auto future = db.loginUserAsync(username, password);
            future.wait(); // 等待异步操作完成
            if (future.get()) {
                return HttpResponse::makeOkResponse("Login Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Login Failed!");
            }
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;
};
