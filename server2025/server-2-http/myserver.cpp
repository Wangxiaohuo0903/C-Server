#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

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

// 新增，解析HTTP请求
std::pair<std::string, std::string> parseHttpRequest(const std::string& request) {
    // 找到第一个空格，确定HTTP方法的结束位置
    size_t method_end = request.find(" ");
    // 提取HTTP方法（如GET、POST）
    std::string method = request.substr(0, method_end);

    // 找到第二个空格，确定URI的结束位置
    size_t uri_end = request.find(" ", method_end + 1);
    // 提取URI（统一资源标识符）
    std::string uri = request.substr(method_end + 1, uri_end - method_end - 1);

    // 返回解析出的HTTP方法和URI
    return {method, uri};
}

// 处理HTTP请求
std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    // 检查GET请求和URI是否在路由表中
    if (method == "GET" && get_routes.count(uri) > 0) {
        // 根据URI调用相应的处理函数
        return get_routes[uri](body);
    }
    // 检查POST请求和URI是否在路由表中
    else if (method == "POST" && post_routes.count(uri) > 0) {
        // 根据URI调用相应的处理函数
        return post_routes[uri](body);
    }
    // 如果请求方法和URI不匹配任何路由，则返回404错误
    else {
        return "404 Not Found";
    }
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // 定义地址
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定socket
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 监听请求
    listen(server_fd, 3);

    // 设置路由
    setupRoutes();

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
