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
    LOG_INFO("Setting up routes");  // 记录路由设置
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

    return {method, uri, body};
}




std::string handleHttpRequest(const std::string& method, const std::string& uri, const std::string& body) {
    LOG_INFO("Handling HTTP request for URI: %s" , uri.c_str());  // 记录请求处理
    if (method == "GET" && get_routes.count(uri) > 0) {
        return get_routes[uri](body);
    } else if (method == "POST" && post_routes.count(uri) > 0) {
        return post_routes[uri](body);
    } else {
        return "404 Not Found";
    }
}



// 设置非阻塞模式的函数
void setNonBlocking(int sock) {
    int opts = fcntl(sock, F_GETFL); // 获取文件描述符的状态标志
    if (opts < 0) {
        LOG_ERROR("fcntl(F_GETFL) failed on socket %d: %s", sock, strerror(errno)); // 记录详细错误信息
        exit(EXIT_FAILURE);
    }
    opts |= O_NONBLOCK; // 设置非阻塞标志
    if (fcntl(sock, F_SETFL, opts) < 0) {
        LOG_ERROR("fcntl(F_SETFL) failed on socket %d: %s", sock, strerror(errno)); // 记录详细错误信息
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Set socket %d to non-blocking", sock);
}


// 处理客户端请求的函数
void handleClientSocket(int client_fd) {
    char buffer[4096];
    std::string request;

    // 读取数据直到没有更多可读数据
    while (true) {
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            request += buffer;
        } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // 没有更多数据可读，退出循环
            break;
        } else {
            // 发生错误或连接关闭
            LOG_ERROR("Read error or connection closed on fd %d", client_fd);
            close(client_fd);
            return;
        }
    }

    // 解析并处理请求
    if (!request.empty()) {
        auto [method, uri, body] = parseHttpRequest(request);
        std::string response_body = handleHttpRequest(method, uri, body);
        std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
        send(client_fd, response.c_str(), response.length(), 0);
    }

    // 关闭客户端连接
    close(client_fd);
    LOG_INFO("Closed connection on fd %d", client_fd);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    struct epoll_event ev, events[MAX_EVENTS]; // epoll_event结构体用于描述事件
    int epollfd;

    // 创建服务器监听套接字
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        LOG_ERROR("Socket creation failed");
        return -1;
    }
    setNonBlocking(server_fd); // 设置服务器套接字为非阻塞模式
    LOG_INFO("Socket created");

    // 定义地址并绑定
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR("Bind failed");
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        LOG_ERROR("Listen failed");
        return -1;
    }
    LOG_INFO("Server listening on port %d", PORT);

    setupRoutes(); // 设置路由表
    LOG_INFO("Routes set up");

    // 创建epoll实例
    epollfd = epoll_create1(0); // 创建一个新的epoll实例
    if (epollfd == -1) {
        LOG_ERROR("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Epoll instance created with fd %d", epollfd);

    // 配置服务器套接字的epoll事件
    ev.events = EPOLLIN | EPOLLET; // 监听可读事件并启用边缘触发
    ev.data.fd = server_fd; // 关联服务器套接字
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        LOG_ERROR("Failed to add server_fd to epoll");
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Server socket added to epoll instance");

    // 主循环，等待事件发生并处理
    while (true) {
        LOG_INFO("Waiting for events...");
        int nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1); // 等待事件
        if (nfds == -1) {
            LOG_ERROR("epoll_wait failed");
            exit(EXIT_FAILURE);
        }
        LOG_INFO("%d events ready", nfds);

        // 遍历发生事件的文件描述符
        for (int n = 0; n < nfds; ++n) {
            if (events[n].data.fd == server_fd) {
                // 如果是服务器套接字，处理新的客户端连接
                LOG_INFO("Server socket event triggered");
                while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) > 0) {
                    setNonBlocking(new_socket); // 设置客户端套接字为非阻塞
                    ev.events = EPOLLIN | EPOLLET; // 监听可读事件，启用边缘触发
                    ev.data.fd = new_socket; // 关联客户端套接字
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, new_socket, &ev) == -1) {
                        LOG_ERROR("Failed to add new socket to epoll");
                        close(new_socket);
                    }
                    LOG_INFO("New connection accepted: fd %d", new_socket);
                }

                if (new_socket == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_WARNING("Accept returned error: %d", errno);
                }
            } else {
                // 如果是客户端套接字，处理客户端请求
                LOG_INFO("Handling client socket event: fd %d", events[n].data.fd);
                handleClientSocket(events[n].data.fd);
            }
        }
    }

    close(server_fd); // 关闭服务器套接字
    LOG_INFO("Server shutdown");
    return 0;
}
