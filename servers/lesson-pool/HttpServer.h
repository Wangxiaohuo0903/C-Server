#pragma once

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
#include "ObjectPool.h"  // 引入内存池

class HttpServer {
public:
    // 构造函数，初始化服务器端口、最大事件数和数据库引用
    HttpServer(int port, int max_events, Database& db) 
        : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {
        // 初始化内存池，预分配100个 HttpRequest 和 HttpResponse 对象
        // 这样可以减少在高并发环境下频繁分配和释放内存带来的开销
        requestPool = std::make_shared<ObjectPool<HttpRequest>>(100);
        responsePool = std::make_shared<ObjectPool<HttpResponse>>(100);
    }

    // 启动服务器，开始监听并处理传入的连接
    void start() {
        setupServerSocket(); // 设置服务器套接字
        setupEpoll();        // 设置 epoll 监听
        ThreadPool pool(16); // 初始化一个包含16个线程的线程池，用于处理高并发请求

        struct epoll_event events[max_events]; // 存储触发事件的数组

        while (true) {
            // 等待事件的发生，-1 表示无限等待
            int nfds = epoll_wait(epollfd, events, max_events, -1);
            for (int n = 0; n < nfds; ++n) {
                if (events[n].data.fd == server_fd) {
                    // 如果事件来自服务器套接字，表示有新的连接请求，进行接受
                    acceptConnection();
                } else {
                    // 否则，事件来自客户端套接字，将处理连接的任务提交给线程池
                    pool.enqueue([fd = events[n].data.fd, this]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

    // 设置路由，将不同的 HTTP 请求路径映射到相应的处理函数
    void setupRoutes() {
        // 添加一个简单的 GET "/" 路由，返回 "Hello, World!" 响应
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });
        
        // 设置与数据库相关的路由
        router.setupDatabaseRoutes(db);
    }

private:
    int server_fd;    // 服务器套接字文件描述符
    int epollfd;      // epoll 实例的文件描述符
    int port;         // 服务器监听的端口号
    int max_events;   // epoll 等待的最大事件数
    Router router;    // 路由器，用于处理不同的 HTTP 请求路径
    Database& db;     // 引用数据库实例

    // 内存池对象，用于管理 HttpRequest 和 HttpResponse 对象的分配和回收
    std::shared_ptr<ObjectPool<HttpRequest>> requestPool;
    std::shared_ptr<ObjectPool<HttpResponse>> responsePool;

    // 设置服务器套接字，包括创建、绑定和监听
    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in address = {};
        address.sin_family = AF_INET;               // 使用 IPv4
        address.sin_addr.s_addr = INADDR_ANY;       // 绑定到所有可用的接口
        address.sin_port = htons(port);             // 设置端口号，转换为网络字节序

        // 设置套接字选项，允许重用地址和端口
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定套接字到指定的地址和端口
        bind(server_fd, (struct sockaddr *)&address, sizeof(address));
        // 开始监听传入的连接，SOMAXCONN 为系统允许的最大连接数
        listen(server_fd, SOMAXCONN);

        // 将套接字设置为非阻塞模式，以支持高并发
        setNonBlocking(server_fd);
    }

    // 设置 epoll，用于高效地监控多个文件描述符的事件
    void setupEpoll() {
        // 创建一个 epoll 实例
        epollfd = epoll_create1(0);
        struct epoll_event event = {};
        event.events = EPOLLIN | EPOLLET; // 监听可读事件，并使用边缘触发模式
        event.data.fd = server_fd;        // 将服务器套接字添加到 epoll 监控中
        epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event);
    }

    // 接受一个新的客户端连接，并将其添加到 epoll 监控中
    void acceptConnection() {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_sock;
        // 使用循环接受所有待处理的连接请求
        while ((client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_addrlen)) > 0) {
            setNonBlocking(client_sock); // 将客户端套接字设置为非阻塞模式
            struct epoll_event event = {};
            event.events = EPOLLIN | EPOLLET; // 监听可读事件，并使用边缘触发模式
            event.data.fd = client_sock;      // 将客户端套接字添加到 epoll 监控中
            epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event);
        }
        // 如果接受连接时出错，且错误不是因为资源暂时不可用，记录错误日志
        if (client_sock == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            LOG_ERROR("Error accepting new connection");
        }
    }

    // 处理一个客户端连接，包括读取请求、解析、生成响应和发送
    void handleConnection(int fd) {
        char buffer[4096];      // 读取数据的缓冲区
        ssize_t bytes_read;     // 实际读取的字节数
        
        // 从请求内存池获取一个 HttpRequest 对象
        auto request = requestPool->acquire();
        // 从响应内存池获取一个 HttpResponse 对象
        auto response = responsePool->acquire();

        // 循环读取客户端发送的数据
        while ((bytes_read = read(fd, buffer, sizeof(buffer)-1)) > 0) {
            buffer[bytes_read] = '\0'; // 确保字符串以 null 结尾
            
            // 解析 HTTP 请求
            if (request->parse(buffer)) {
                // 路由请求，生成相应的 HTTP 响应
                *response = router.routeRequest(*request);
                // 将响应对象转换为字符串
                std::string response_str = response->toString();
                // 发送响应给客户端
                send(fd, response_str.c_str(), response_str.length(), 0);
                
                // 重置对象以便下次使用，确保对象状态干净
                request->reset();
                response->reset();
            }
        }
        
        // 如果读取数据时出错，且错误不是因为资源暂时不可用，记录错误日志
        if (bytes_read == -1 && errno != EAGAIN) {
            LOG_ERROR("Error reading from socket %d", fd);
        }
        // 关闭客户端套接字，结束连接
        close(fd);
    }

    // 将一个套接字设置为非阻塞模式
    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0); // 获取当前文件描述符的标志
        flags |= O_NONBLOCK;                 // 添加非阻塞标志
        fcntl(sock, F_SETFL, flags);         // 设置新的标志
    }
};
