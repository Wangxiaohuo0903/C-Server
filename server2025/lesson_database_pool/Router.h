#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include <fstream>      // 用于文件读写
#include <filesystem>   // C++17, 用于检查文件存在、创建目录等
#include <sstream>
// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router {
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    // 添加路由：将 HTTP 方法和路径映射到处理函数
    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
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
    void setupFileRoutes(const std::string& uploadDir = "uploads") {


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
   
        addRoute("GET", "/index", [this](const HttpRequest& req) {
                // 假设 index.html 与可执行程序位于同一目录
                const std::string filename = "UI/index.html";

                std::ifstream ifs(filename);
                if (!ifs.is_open()) {
                    // 如果文件没找到或读取失败，返回 404
                    return HttpResponse::makeErrorResponse(404, "index.html Not Found");
                }

                // 读取文件全部内容
                std::stringstream buffer;
                buffer << ifs.rdbuf();
                ifs.close();

                // 构造响应
                HttpResponse resp(200);
                // 告诉浏览器这是一个 HTML 文档
                resp.setHeader("Content-Type", "text/html; charset=UTF-8");
                resp.setBody(buffer.str());
                return resp;
            });
            }
private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 存储路由映射
};
