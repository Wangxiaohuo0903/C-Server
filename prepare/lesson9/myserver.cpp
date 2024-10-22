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
#define MAX_EVENTS 10

using RequestHandler = std::function<std::string(const std::string&)>;

std::map<std::string, RequestHandler> get_routes;
std::map<std::string, RequestHandler> post_routes;
Database db("users.db");


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
    int opts;
    opts = fcntl(sock, F_GETFL); // 获取socket的文件描述符当前的状态标志
    if (opts < 0) {
        LOG_ERROR("fcntl(F_GETFL)"); // 获取标志失败，记录错误日志
        exit(EXIT_FAILURE);
    }
    opts = (opts | O_NONBLOCK); // 设置非阻塞标志
    if (fcntl(sock, F_SETFL, opts) < 0) { // 应用新的标志设置到socket
        LOG_ERROR("fcntl(F_SETFL)"); // 设置失败，记录错误日志
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
    server_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建TCP socket
    setNonBlocking(server_fd); // 设置为非阻塞模式
    LOG_INFO("Socket created");

    // 定义地址
    address.sin_family = AF_INET; // 指定地址族为IPv4
    address.sin_addr.s_addr = INADDR_ANY; // 绑定到所有可用地址
    address.sin_port = htons(PORT); // 指定端口号

    // 绑定socket
    bind(server_fd, (struct sockaddr *)&address, sizeof(address)); // 绑定socket到指定地址和端口

    // 监听请求
    listen(server_fd, 3); // 开始监听端口，设置最大监听队列长度为3
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
    // epoll_wait函数等待epoll描述符上注册的文件描述符有IO事件发生，nfds表示实际发生的事件数量，
    // events是用于存储事件信息的缓冲区，MAX_EVENTS定义了缓冲区大小，-1表示无限期等待。
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1); 

    // 遍历所有已发生的事件
    for (int n = 0; n < nfds; ++n) {
        // 检查是否为服务器监听套接字（server_fd）上的新连接事件
        if (events[n].data.fd == server_fd) {
            // 接受新的客户端连接请求，并返回新创建的连接套接字
            new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

            // 设置新连接为非阻塞模式，以便后续在epoll中进行异步I/O操作
            setNonBlocking(new_socket);

            // 初始化一个新的epoll事件结构体ev，设置监听新连接的可读事件和边缘触发模式
            ev.events = EPOLLIN | EPOLLET;

            // 将新连接的套接字赋值给事件结构体中的数据字段
            ev.data.fd = new_socket;

            // 使用epoll_ctl将新连接添加到epoll的监控列表中
            if (epoll_ctl(vepollfd, EPOLL_CTL_ADD, new_socket, &e) == -1) {
                // 如果注册新连接事件失败，则记录错误日志并退出程序
                LOG_ERROR("epoll_ctl: new_socket");
                exit(EXIT_FAILURE);
            }
        } 
        // 处理已有连接的事件
        else {
            // 获取当前事件对应的已连接套接字
            int client_fd = events[n].data.fd;

            // 定义一个缓冲区来接收从客户端读取的数据
            char buffer[4096] = {0};

            // 循环处理已连接socket的读取操作，直到没有更多数据或发生错误
            while (true) {
                // 从已连接套接字读取数据到缓冲区
                ssize_t count = read(client_fd, buffer, sizeof(buffer));

                // 如果读取失败且错误不是EAGAIN（资源暂时不可用），则记录错误并关闭连接
                if (count == -1 && errno != EAGAIN) {
                    LOG_ERROR("Read error");
                    close(client_fd);
                    break;
                }

                // 如果读取到的数据长度为0，说明连接已被对方关闭，关闭本端连接并跳出循环
                if (count == 0) {
                    close(client_fd);
                    break;
                }

                // 如果成功读取到数据，解析HTTP请求，并处理请求
                if (count > 0) {
                    std::string request(buffer);
                    auto [method, uri, body] = parseHttpRequest(request);

                    // 调用处理函数生成响应内容
                    std::string response_body = handleHttpRequest(method, uri, body);

                    // 构造HTTP响应头与响应体，并发送回客户端
                    std::string response = "HTTP/1.1 200 OK\nContent-Type: text/plain\n\n" + response_body;
                    send(client_fd, response.c_str(), response.size(), 0);

                    // 这里直接关闭了连接，实际情况可能需要根据HTTP协议保持长连接或者按照Keep-Alive策略处理
                    close(client_fd);
                }
            }
        }
    }
}
    return 0;
}
