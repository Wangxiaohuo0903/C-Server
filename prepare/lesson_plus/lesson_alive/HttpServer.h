#pragma once

// 导入必要的头文件
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "Logger.h" // 日志功能
#include "ThreadPool.h" // 线程池处理并发
#include "Router.h" // 路由请求到不同的处理器
#include "HttpRequest.h" // 解析HTTP请求
#include "HttpResponse.h" // 构造HTTP响应
#include "Database.h" // 数据库交互

// 定义 HttpServer 类
class HttpServer {
public:
    // 构造函数初始化服务器，指定监听端口、最大事件数和数据库引用
    HttpServer(int port, int max_events, Database& db) 
        : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {}

    // 启动服务器的主函数
    void start() {
        setupServerSocket(); // 设置服务器套接字
        setupEpoll(); // 设置epoll事件监听
        ThreadPool pool(16); // 初始化一个有16个线程的线程池

        struct epoll_event events[max_events]; // 存储epoll事件的数组

        // 无限循环监听事件
        while (true) {
            // 等待epoll事件，无超时
            int nfds = epoll_wait(epollfd, events, max_events, -1);
            for (int n = 0; n < nfds; ++n) {
                // 检查是否为新的连接请求
                if (events[n].data.fd == server_fd) {
                    acceptConnection(); // 接受新连接
                } else {
                    // 对于已建立的连接，异步处理请求
                    pool.enqueue([fd = events[n].data.fd, this]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

    // 设置路由规则
    void setupRoutes() {
        // 设置默认的 GET 路径 "/"
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });
        // 可以添加更多的路由规则
        router.setupDatabaseRoutes(db); // 设置数据库相关的路由
    }

private:
    int server_fd, epollfd, port, max_events; // 服务器套接字、epoll 文件描述符、端口号和最大事件数
    Router router; // 请求路由器
    Database& db; // 数据库引用

    // 设置服务器套接字，监听指定端口
    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建套接字
        struct sockaddr_in address = {}; // 地址结构
        address.sin_family = AF_INET; // IPv4
        address.sin_addr.s_addr = INADDR_ANY; // 任意地址
        address.sin_port = htons(port); // 端口号

        int opt = 1;
        // 设置套接字选项，允许地址重用
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定地址
        bind(server_fd, (struct sockaddr *)&address, sizeof(address));
        // 开始监听
        listen(server_fd, SOMAXCONN);

        // 设置非阻塞模式
        setNonBlocking(server_fd);
    }

    // 设置 epoll 事件监听
    void setupEpoll() {
        epollfd = epoll_create1(0); // 创建 epoll 实例
        struct epoll_event event = {}; // epoll 事件
        event.events = EPOLLIN | EPOLLET; // 监听读事件，边缘触发模式
        event.data.fd = server_fd; // 监听服务器套接字
        // 添加服务器套接字到 epoll 监听
        epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event);
    }

    // 接受新的连接请求
    void acceptConnection() {
        struct sockaddr_in client_addr; // 客户端地址
        socklen_t client_addrlen = sizeof(client_addr); // 地址长度
        int client_sock; // 客户端套接字
        // 循环接受所有等待的连接请求
        while ((client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_addrlen)) > 0) {
            // 设置客户端套接字为非阻塞模式
            setNonBlocking(client_sock);
            struct epoll_event event = {}; // 创建新的 epoll 事件
            event.events = EPOLLIN | EPOLLET; // 监听读事件，边缘触发模式
            event.data.fd = client_sock; // 设置客户端套接字
            // 将客户端套接字添加到 epoll 监听
            epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event);
        }
        // 处理 accept 函数的错误
        if (client_sock == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            LOG_ERROR("Error accepting new connection");
        }
    }

    // 处理连接请求，读取数据并响应
    void handleConnection(int fd) {
        char buffer[4096]; // 读取缓冲区
        ssize_t bytes_read; // 读取的字节数
        HttpRequest request; // HTTP请求对象
        bool keepAlive = false; // 是否保持连接
        bool headerParsed = false; // 请求头是否已解析完成

        // 循环读取数据
        while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0'; // 确保以 null 结尾
            if (!headerParsed) {
                // 如果请求头还未解析完成，尝试解析
                if (request.append(buffer)) {
                    // 检查是否解析到请求体或请求解析完成
                    if (request.getState() == HttpRequest::BODY || request.getState() == HttpRequest::FINISH) {
                        headerParsed = true; // 标记请求头解析完成
                        keepAlive = request.isKeepAlive(); // 检查是否保持连接
                    }
                }
            }

            // 如果请求头解析完成，处理请求并响应
            if (headerParsed || request.getMethod() == HttpRequest::GET) {
                HttpResponse response = router.routeRequest(request); // 根据请求路由处理
                if (keepAlive) {
                    response.setHeader("Connection", "keep-alive"); // 设置保持连接
                } else {
                    response.setHeader("Connection", "close"); // 设置关闭连接
                }
                if (request.acceptsGzip()) {
                    response.compressBody(); // 压缩响应体
                }
                std::string response_str = response.toString(); // 将响应转化为字符串
                send(fd, response_str.c_str(), response_str.length(), 0); // 发送响应
                if (!keepAlive) {
                    break; // 如果不保持连接，退出循环
                }
                // 准备处理下一个请求，重置状态
                request = HttpRequest(); // 重置请求对象以处理新的请求
                headerParsed = false; // 重置解析状态
            }
        }

        // 如果不保持连接或读取出错，关闭连接
        if (!keepAlive || (bytes_read == -1 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close(fd); // 关闭文件描述符
        }
    }

    // 设置套接字为非阻塞模式
    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0); // 获取当前标志
        flags |= O_NONBLOCK; // 添加非阻塞标志
        fcntl(sock, F_SETFL, flags); // 设置新的标志
    }
};
