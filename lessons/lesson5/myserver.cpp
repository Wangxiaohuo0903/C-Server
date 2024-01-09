#include <iostream>  // 引入标准输入输出库
#include <map>       // 引入标准映射容器库
#include <functional>// 引入函数对象库，用于定义函数类型
#include <string>    // 引入字符串处理库
#include <sys/socket.h> // 引入socket编程接口
#include <stdlib.h>  // 引入标准库，用于通用工具函数
#include <netinet/in.h> // 引入网络字节序转换函数
#include <string.h>  // 引入字符串处理函数
#include <unistd.h>  // 引入UNIX标准函数库


// 测试命令 curl http://localhost:8081/register
#define PORT 8080  // 定义监听端口号为8080

using RequestHandler = std::function<std::string(const std::string&)>; // 定义请求处理函数类型

std::map<std::string, RequestHandler> route_table; // 定义路由表，映射路径到对应的处理函数


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
    int server_fd, new_socket; // 声明服务器socket和新连接的socket
    struct sockaddr_in address; // 声明网络地址结构
    int addrlen = sizeof(address); // 获取地址结构的长度

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    // 定义服务器地址
    address.sin_family = AF_INET;         // 设置地址族为IPv4
    address.sin_addr.s_addr = INADDR_ANY; // 绑定到所有可用网络接口
    address.sin_port = htons(PORT);       // 设置服务器端口号

    // 将socket绑定到地址
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 监听端口上的网络请求
    listen(server_fd, 3);

    // 初始化路由表
    setupRoutes();

    while (true) {
        // 接受来自客户端的连接
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        // 读取客户端的请求数据
        char buffer[1024] = {0};
        read(new_socket, buffer, 1024);
        std::string request(buffer);
        
        // 解析请求的URI
        std::string uri = request.substr(request.find(" ") + 1);
        uri = uri.substr(0, uri.find(" "));

        // 根据路由表处理请求并生成响应
        std::string response_body;
        if (route_table.count(uri) > 0) {
            response_body = route_table[uri](request);
        } else {
            response_body = "404 Not Found";
        }

        // 向客户端发送HTTP响应
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
