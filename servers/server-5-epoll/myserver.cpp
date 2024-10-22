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

#define PORT 8080
#define MAX_EVENTS 100

using RequestHandler = std::function<std::string(const std::string&)>;

std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;
Database db("users.db");

void logError(const char* msg) {
    perror(msg);
    LOG_ERROR("%s", msg);
}
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

    LOG_INFO("Parsing body: %s" , body.c_str());  // 记录原始body数据

    while (std::getline(stream, pair, '&')) {
        std::string::size_type pos = pair.find('=');
        if (pos != std::string::npos) {
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;

            LOG_INFO("Parsed key-value pair: %s = %s" ,key.c_str(), value.c_str());  // 记录每个解析出的键值对
        } else {
            // 错误处理：找不到 '=' 分隔符
            std::string error_msg = "Error parsing: " + pair;
            LOG_ERROR(error_msg.c_str());  // 记录错误信息
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




std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    LOG_INFO("Handling HTTP request for URI: %s" , uri.c_str());  // 第七课新增：记录请求处理
    if (method == "GET" && get_routes.count(uri) > 0) {
        return get_routes[uri](body);
    } else if (method == "POST" && post_routes.count(uri) > 0) {
        return post_routes[uri](body);
    } else {
        return "404 Not Found";
    }
}




//第九课新增

void setNonBlocking(int sock) {
    int opts = fcntl(sock, F_GETFL);
    if (opts < 0) {
        logError("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    opts |= O_NONBLOCK;
    if (fcntl(sock, F_SETFL, opts) < 0) {
        logError("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Set socket %d to non-blocking", sock);
}

void handleClientSocket(int client_fd) {
    char buffer[4096];
    std::string request;

    // 读取数据直到遇到EAGAIN
    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            request += buffer;
        } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 没有更多数据可读
            break;
        } else {
            // 出错或连接被关闭
            LOG_ERROR("Read error or connection closed on fd %d", client_fd);
            close(client_fd);
            return;
        }
    }

    if (!request.empty()) {
        auto [method, uri, body] = parseHttpRequest(request);
        std::string response_body = handleHttpRequest(method, uri, body);
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(client_fd, response.c_str(), response.length(), 0);
    }

    // 关闭套接字
    close(client_fd);
    LOG_INFO("Closed connection on fd %d", client_fd);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    struct epoll_event ev, events[MAX_EVENTS];
    int epollfd, nfds;

    // 创建socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建TCP socket
    if (server_fd == -1) {
        logError("Socket creation failed");
        return -1;
    }
    setNonBlocking(server_fd); // 设置为非阻塞模式
    LOG_INFO("Socket created");

    // 定义地址
    address.sin_family = AF_INET; // 指定地址族为IPv4
    address.sin_addr.s_addr = INADDR_ANY; // 绑定到所有可用地址
    address.sin_port = htons(PORT); // 指定端口号

    // 绑定socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        logError("Bind failed");
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        logError("Listen failed");
        return -1;
    }
    LOG_INFO("Server listening on port %d" , PORT);

     // 设置路由
    setupRoutes();
    LOG_INFO("Server starting");

    // 创建epoll实例
    epollfd = epoll_create1(0); // 创建epoll实例
    if (epollfd == -1) {
        LOG_ERROR("epoll_create -1"); // 创建失败，记录错误日志
        exit(EXIT_FAILURE);
    }

    ev.events = EPOLLIN | EPOLLET; // 设置要监听的事件类型：可读和边缘触发
    ev.data.fd = server_fd;
    /*
    使用epoll_ctl函数将刚刚配置好的事件注册到epoll实例中。这里的参数意义如下：
    epollfd: 是之前创建的epoll实例的文件描述符；
    EPOLL_CTL_ADD: 指令标识，表示向epoll实例添加一个新的需要监控的文件描述符及其事件；
    server_fd: 需要监控的文件描述符，也就是服务器监听套接字；
    &ev: 指向包含事件类型和其他相关信息的epoll_event结构体的指针。
    */
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        LOG_ERROR("epoll_ctl: server_fd"); // 注册事件失败，记录错误日志
        exit(EXIT_FAILURE);
    }

// 主循环，持续监听事件发生

  while (true) {
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                // 接受新连接
                new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    setNonBlocking(new_socket);
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = new_socket;
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, new_socket, &ev) < 0) {
                        logError("epoll_ctl - new socket");
                        close(new_socket);
                    } else {
                        LOG_INFO("New connection accepted: fd %d", new_socket);
                    }
                
                if (new_socket == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    logError("accept");
                }
            } else {
                // 处理客户端请求
                handleClientSocket(events[n].data.fd);
            }
        }
    }
    close(server_fd);

    return 0;
}
