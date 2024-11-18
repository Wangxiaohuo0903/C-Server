#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#include "Logger.h"

#define PORT 8080

// 请求处理函数类型定义
using RequestHandler = std::function<std::string(const std::string&)>;

// 分别为GET和POST请求设置路由表
std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;

// 初始化路由表
void setupRoutes() {
    LOG_INFO("Setting up routes");  
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

    // POST请求处理
    post_routes["/register"] = [](const std::string& request) {
        // TODO: 实现用户注册逻辑
        return "Register Success!";
    };
    post_routes["/login"] = [](const std::string& request) {
        // TODO: 实现用户登录逻辑
        return "Login Success!";
    };

    // TODO: 添加其他路径和处理函数
}

// 解析HTTP请求
std::pair<std::string, std::string> parseHttpRequest(const std::string& request) {
    LOG_INFO("Parsing HTTP request");  // 记录请求解析
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

// 处理HTTP请求
std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    LOG_INFO("Handling HTTP request for URI: %s", uri.c_str());  // 记录请求处理
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

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    LOG_INFO("Socket created"); 

    // 定义地址
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定socket
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 监听请求
    listen(server_fd, 3);
    LOG_INFO("Server listening on port %d",PORT);  // 记录服务器监听


    // 设置路由
    setupRoutes();
    LOG_INFO("Server starting");
    while (true) {
        // 接受连接
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        // 读取请求
        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        std::string request(buffer);
        
        // 解析请求
        auto [method, uri] = parseHttpRequest(request);

        // 处理请求
        std::string response_body = handleHttpRequest(method, uri, request);

        // 发送响应
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(new_socket, response.c_str(), response.size(), 0);

        // 关闭连接
        close(new_socket);
    }

    return 0;
}
