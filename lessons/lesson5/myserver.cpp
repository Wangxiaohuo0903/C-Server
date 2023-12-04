#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

// 测试命令 curl http://localhost:8081/register
#define PORT 8080

// 请求处理函数类型定义
using RequestHandler = std::function<std::string(const std::string&)>;

// 路由表
std::map<std::string, RequestHandler> route_table;

// 初始化路由表
void setupRoutes() {
    // 根路径处理
    route_table["/"] = [](const std::string& request) {
        return "Hello, World!";
    };

    // 注册处理
    route_table["/register"] = [](const std::string& request) {
        // TODO: 实现用户注册逻辑
        return "Register Success!";
    };

    // 登录处理
    route_table["/login"] = [](const std::string& request) {
        // TODO: 实现用户登录逻辑
        return "Login Success!";
    };

    // TODO: 添加其他路径和处理函数
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // 定义地址
    address.sin_family = AF_INET; // 使用IPv4地址
    address.sin_addr.s_addr = INADDR_ANY; // 绑定到所有网络接口
    address.sin_port = htons(PORT); // 设置端口号

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
        
        // 解析URI
        std::string uri = request.substr(request.find(" ") + 1);
        uri = uri.substr(0, uri.find(" "));

        // 根据路由表处理请求
        std::string response_body;
        if (route_table.count(uri) > 0) {
            response_body = route_table[uri](request);
        } else {
            response_body = "404 Not Found"; // 未找到对应路径时的响应
        }

        // 发送响应
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(new_socket, response.c_str(), response.size(), 0);

        // TODO: 实现多线程处理来提高性能
        // TODO: 添加日志系统以记录请求和响应
        // TODO: 实现更完善的错误处理机制

        // 关闭连接
        close(new_socket);
    }

    return 0;
}
