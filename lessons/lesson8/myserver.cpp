#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <iomanip>
#include <unistd.h>

#include <sstream>
#include "Logger.h"
#include "Database.h"
#define PORT 8080

// 请求处理函数类型定义
using RequestHandler = std::function<std::string(const std::string&)>;

// 分别为GET和POST请求设置路由表
std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;
Database db("users.db"); // 创建数据库对象


// 第八课新增：解析请求正文的辅助函数
std::string urlDecode(const std::string &s) {
    std::string decoded;
    std::istringstream iss(s);
    char c;

    while (iss >> c) {
        if (c == '+') {
            decoded += ' ';
        } else if (c == '%' && iss.peek() != EOF) {
            int value;
            iss >> std::hex >> value;
            decoded += static_cast<char>(value);
            iss.get(); // 移动到下一个字符
        } else {
            decoded += c;
        }
    }

    return decoded;
}

// 然后在 parseFormBody 函数中使用它
std::map<std::string, std::string> parseFormBody(const std::string& body) {
    std::map<std::string, std::string> params;
    std::istringstream stream(body);
    std::string pair;

    LOG_INFO("Parsing body: " + body);  // 记录原始body数据

    while (std::getline(stream, pair, '&')) {
        std::string::size_type pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;

            LOG_INFO("Parsed key-value pair: " + key + " = " + value);  // 记录每个解析出的键值对
        } else {
            // 错误处理：找不到 '=' 分隔符
            std::string error_msg = "Error parsing: " + pair;
            LOG_ERROR(error_msg);  // 记录错误信息
            std::cerr << error_msg << std::endl;
        }
    }
    return params;
}

// 初始化路由表
void setupRoutes() {
    LOG_INFO("Setting up routes");  // 第七课新增：记录路由设置
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

    // 修改POST路由逻辑
    post_routes["/register"] = [](const std::string& request) {
        // 解析用户名和密码
        // 例如从请求中解析 username 和 password，这里需要您自己实现解析逻辑
        auto params = parseFormBody(request);
        std::string username = params["username"];
        std::string password = params["password"];

        // 调用数据库方法进行注册
        if (db.registerUser(username, password)) {
            return "Register Success!";
        } else {
            return "Register Failed!";
        }
    };

    // 登录路由
    post_routes["/login"] = [](const std::string& request) {
        // 解析用户名和密码
        auto params = parseFormBody(request);
        std::string username = params["username"];
        std::string password = params["password"];

        // 调用数据库方法进行登录
        if (db.loginUser(username, password)) {
            return "Login Success!";
        } else {
            return "Login Failed!";
        }
    };
     // TODO: 添加其他路径和处理函数
}

// 第六课新增，解析HTTP请求
std::tuple<std::string, std::string, std::string> parseHttpRequest(const std::string& request) {
    LOG_INFO("Parsing HTTP request");  // 第七课新增：记录请求解析
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

    return {method, uri, body};
}



// 第六课新增，处理HTTP请求
std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    LOG_INFO("Handling HTTP request for URI: " + uri);  // 第七课新增：记录请求处理
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
    LOG_INFO("Server listening on port " + std::to_string(PORT));  // 第七课新增：记录服务器监听


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
        auto [method, uri, body] = parseHttpRequest(request);

        // 处理请求
        std::string response_body = handleHttpRequest(method, uri, body);

        // 发送响应
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(new_socket, response.c_str(), response.size(), 0);

        // 关闭连接
        close(new_socket);
    }

    return 0;
}
