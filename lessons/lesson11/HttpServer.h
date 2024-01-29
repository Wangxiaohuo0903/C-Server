// HttpServer.h
#pragma once

// 引入所需的头文件
#include <sys/socket.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include "Logger.h"
#include "ThreadPool.h"
#include "Router.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"

// 定义 HttpServer 类
class HttpServer {
public:
    // 构造函数，初始化服务器
    HttpServer(int port, int max_events, Database& db) : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {}

    // 启动服务器的方法
    void start() {
        // 设置服务器 socket 和 epoll
        setupServerSocket();
        setupEpoll();
        // 创建一个含有 4 个线程的线程池
        ThreadPool pool(4);

        // 创建一个 epoll 事件数组
        struct epoll_event events[max_events];

        // 主循环，不断检查事件
        while (true) {
            // 等待 epoll 事件
            int nfds = epoll_wait(epollfd, events, max_events, -1);
            for (int n = 0; n < nfds; ++n) {
                // 如果事件是新的连接请求
                if (events[n].data.fd == server_fd) {
                    // 接受新的连接
                    acceptConnection();
                } else {
                    // 否则，处理已有连接的 IO 事件，将任务添加到线程池
                    pool.enqueue([fd = events[n].data.fd, this]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

    // 设置路由
    void setupRoutes() {
        // 添加 GET 请求的路由处理
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });
        // 添加数据库相关的路由
        router.setupDatabaseRoutes(db);
        // ... 其他路由可以在这里添加 ...
    }

private:
    // 私有成员变量：服务器文件描述符、epoll 文件描述符、端口、最大事件数、路由器和数据库引用
    int server_fd, epollfd, port, max_events;
    Router router;
    Database& db;

    // 设置服务器 socket 的方法
    void setupServerSocket() {
        // 创建 socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        // 设置 socket 地址
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        // 设置 socket 选项，例如地址重用
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定 socket 到指定地址和端口
        bind(server_fd, (struct sockaddr *)&address, sizeof(address));
        // 开始监听端口
        listen(server_fd, SOMAXCONN);

        // 设置 socket 为非阻塞模式
        setNonBlocking(server_fd);
    }

    // 设置 epoll 的方法
    void setupEpoll() {
        // 创建 epoll 实例
        epollfd = epoll_create1(0);
        // 设置 epoll 事件
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        // 将服务器 socket 添加到 epoll 监控
        epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event);
    }

    // 接受新连接的方法
    void acceptConnection() {
        // 定义客户端地址结构
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        // 接受新的客户端连接
        int client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_addrlen);
        // 设置客户端 socket 为非阻塞模式
        setNonBlocking(client_sock);
        // 设置 epoll 事件
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = client_sock;
        // 将新的客户端 socket 添加到 epoll 监控
        epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event);
    }

    // 处理连接的方法
    void handleConnection(int fd) {
        // 创建缓冲区
        char buffer[4096];
        // 读取数据
        int bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            // 如果没有读到数据或出错，关闭 socket
            close(fd);
            return;
        }
        // 结束字符串
        buffer[bytes_read] = '\0';

        // 解析 HTTP 请求
        HttpRequest request;
        if (request.parse(buffer)) {
            // 如果请求解析成功，通过路由处理请求并生成响应
            HttpResponse response = router.routeRequest(request);
            // 将响应转换为字符串
            std::string response_str = response.toString();
            // 发送响应
            send(fd, response_str.c_str(), response_str.length(), 0);
        }
        // 关闭 socket
        close(fd);
    }

    // 设置非阻塞模式的辅助函数
    void setNonBlocking(int sock) {
        // 获取文件描述符的标志
        int flags = fcntl(sock, F_GETFL, 0);
        // 设置非阻塞标志
        flags |= O_NONBLOCK;
        // 应用标志设置
        fcntl(sock, F_SETFL, flags);
    }
};
