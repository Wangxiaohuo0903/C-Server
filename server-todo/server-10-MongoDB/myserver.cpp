#include <iostream>
#include <map>
#include <functional>
#include <string>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include "Logger.h"
#include "Database.h"
#include "ThreadPool.h"
#include "HttpRequest.h"
#include "HttpResponse.h"

#define PORT 8080
#define MAX_EVENTS 10

using RequestHandler = std::function<std::string(const std::string&)>;

std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;
Database db("mongodb://localhost:27017");


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

    // LOG_INFO("Parsing body: " + body);  // 记录原始body数据

    while (std::getline(stream, pair, '&')) {
        std::string::size_type pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;

            // LOG_INFO("Parsed key-value pair: " + key + " = " + value);  // 记录每个解析出的键值对
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





void setNonBlocking(int sock) {
    int opts;
    opts = fcntl(sock, F_GETFL);
    if (opts < 0) {
        LOG_ERROR("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(sock, F_SETFL, opts) < 0) {
        LOG_ERROR("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    struct epoll_event ev, events[MAX_EVENTS];
    int epollfd, nfds;

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setNonBlocking(server_fd); // 设置非阻塞
    LOG_INFO("Socket created");

    // 定义地址
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定socket
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));

    // 监听请求
    listen(server_fd, 3);
    LOG_INFO("Server listening on port " + std::to_string(PORT));

    // 创建epoll实例
    epollfd = epoll_create1(0);
    if (epollfd == -1) {
        LOG_ERROR("epoll_create1");
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN | EPOLLET; // 监听读事件和边缘触发
    ev.data.fd = server_fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        LOG_ERROR("epoll_ctl: server_fd");
        exit(EXIT_FAILURE);
    }

    // 设置路由
    setupRoutes();
    LOG_INFO("Server starting");


    ThreadPool pool(4); // 创建含有4个线程的线程池
    LOG_INFO("Thread pool created with 4 threads");

    while (true) {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                // 处理新连接
                new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
                if (new_socket == -1) {
                    LOG_ERROR("Error accepting new connection");
                    continue;
                }

                setNonBlocking(new_socket);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = new_socket;
                if (epoll_ctl(epollfd, EPOLL_CTL_ADD, new_socket, &ev) == -1) {
                    LOG_ERROR("Error adding new socket to epoll");
                    close(new_socket);
                } else {
                    LOG_INFO("New connection accepted, socket added to epoll");
                }
            } else {
                // 将任务加入线程池
                pool.enqueue([fd = events[n].data.fd]() {
                    LOG_INFO("Handling request on socket: " + std::to_string(fd));
                    char buffer[1024] = {0};
                    read(fd, buffer, 1024);
                    std::string request(buffer);
                    auto [method, uri, body] = parseHttpRequest(request);
                    std::string response_body = handleHttpRequest(method, uri, body);
                    std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
                    send(fd, response.c_str(), response.size(), 0);
                    close(fd);
                    LOG_INFO("Request handled and response sent on socket: " + std::to_string(fd));
                });
                LOG_INFO("Task added to thread pool for socket: " + std::to_string(events[n].data.fd));
            }
        }
    }
    return 0;
}