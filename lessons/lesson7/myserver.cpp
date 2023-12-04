#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

// 第七节课新增
#include "log.h"
#include "DatabaseManager.h"
// 全局变量
DatabaseManager dbManager("database.db");

#define PORT 8080

// 请求处理函数类型定义
using RequestHandler = std::function<std::string(const std::string&)>;

// 分别为GET和POST请求设置路由表
std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;

// 初始化路由表
void setupRoutes() {
    // GET请求处理
    get_routes["/"] = [](const std::string& request) {
        return "Hello, World!";
    };
    get_routes["/register"] = [](const std::string& request) {
        // TODO: 实现用户注册逻辑
        return "Please use POST to register";
    };
    get_routes["/login"] = [](const std::string& request) {
        // TODO: 实现用户登录逻辑
        return "Please use POST to login";
    };

    // POST请求处理 第七课新增
    post_routes["/register"] = [](const std::string& request) {
        // 解析用户名和密码
        size_t username_pos = request.find("username=");
        size_t password_pos = request.find("&password=");
        
        if (username_pos == std::string::npos || password_pos == std::string::npos) {
            return "Invalid request format";
        }

        std::string username = request.substr(username_pos + 9, password_pos - (username_pos + 9));
        std::string password = request.substr(password_pos + 10);

        if (dbManager.createUser(username, password)) {
            return "Register Success!";
        } else {
            return "Register Failed!";
        }
    };
    post_routes["/login"] = [](const std::string& request) {
        // 解析用户名和密码
        size_t username_pos = request.find("username=");
        size_t password_pos = request.find("&password=");

        if (username_pos == std::string::npos || password_pos == std::string::npos) {
            return "Invalid request format";
        }

        std::string username = request.substr(username_pos + 9, password_pos - (username_pos + 9));
        std::string password = request.substr(password_pos + 10);

        if (dbManager.validateUser(username, password)) {
            return "Login Success!";
        } else {
            return "Login Failed!";
        }
    };

    // TODO: 添加其他路径和处理函数
}

// 第六课新增，解析HTTP请求
std::pair<std::string, std::string> parseHttpRequest(const std::string& request) {
    // 解析请求方法和URI
    size_t method_end = request.find(" ");
    std::string method = request.substr(0, method_end);
    size_t uri_end = request.find(" ", method_end + 1);
    std::string uri = request.substr(method_end + 1, uri_end - method_end - 1);

    // 提取请求体（对于POST请求）
    std::string body;
    if (method == "POST") {
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            body = request.substr(body_start + 4);
        }
    }

    return {method, uri};
}


std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    if (method == "GET" && get_routes.count(uri) > 0) {
        return get_routes[uri](body);
    } else if (method == "POST" && post_routes.count(uri) > 0) {
        return post_routes[uri](body);
    } else {
        return "404 Not Found";
    }
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);

    setupRoutes();
    LOG_INFO("Server starting...");

    while (true) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        LOG_INFO("New connection accepted");

        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        LOG_INFO("Request received: " + std::string(buffer));

        auto [method, uri] = parseHttpRequest(buffer);
        std::string response_body = handleHttpRequest(method, uri, buffer);
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(new_socket, response.c_str(), response.size(), 0);
        LOG_INFO("Response sent");

        close(new_socket);
    }

    return 0;
}