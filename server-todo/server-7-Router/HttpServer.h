// #pragma once 是预处理指令，确保头文件在多次包含时只会被编译一次，防止重复定义问题

#include <sys/socket.h> // 引入系统socket函数库，用于创建、绑定和监听套接字等操作
#include <sys/epoll.h> // 引入epoll事件驱动机制的函数库，用于实现高效的IO多路复用
#include <fcntl.h> // 引入文件描述符标志控制函数库，如fcntl()函数
#include <netinet/in.h> // 引入IPv4地址族的网络编程接口，如sockaddr_in结构体
#include <unistd.h> // 引入Unix标准函数库，提供close、read、write等基本系统调用
#include <cstring> // 引入C字符串操作函数库
#include "Logger.h" // 自定义日志模块，用于记录服务器运行过程中的信息
#include "ThreadPool.h" // 引入线程池模块，用于并发处理客户端连接请求
#include "Router.h" // 引入路由模块，根据HTTP请求的方法和路径分发到不同的处理器
#include "HttpRequest.h" // 引入HTTP请求解析类，用于解析客户端发送过来的请求数据
#include "HttpResponse.h" // 引入HTTP响应构建类，用于构建服务端返回给客户端的响应数据
#include "Database.h" // 引入库，提供与数据库交互的功能

class HttpServer {
public:
    // 构造函数，初始化成员变量并传入参数（端口号、epoll的最大监听事件数以及数据库引用）
    HttpServer(int port, int max_events, Database& db) 
        : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {}

    // 启动服务器方法，设置套接字、epoll、启动线程池并进入循环等待处理客户端连接
    void start() {
        setupServerSocket(); // 创建并配置服务器套接字
        setupEpoll(); // 创建并配置epoll实例
        ThreadPool pool(16); // 创建一个拥有16个工作线程的线程池以应对高并发场景
        
        // 初始化epoll_event数组，用于存放epoll_wait返回的就绪事件
        struct epoll_event events[max_events];

        // 主循环，不断等待新的连接请求或已连接套接字上的读写事件
        while (true) {
            int nfds = epoll_wait(epollfd, events, max_events, -1); // 等待epoll事件发生
            
            // 遍历所有就绪事件
            for (int n = 0; n < nfds; ++n) {
                if (events[n].data.fd == server_fd) { // 如果是服务器套接字就绪
                    acceptConnection(); // 接受新连接
                } else { // 如果是已连接客户端套接字就绪
                    // 使用线程池异步处理该连接上的请求
                    pool.enqueue([fd = events[n].data.fd, this]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

    // 设置服务器路由映射表的方法
    void setupRoutes() {
        // 添加根路由处理器，返回简单的Hello World!响应
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });

        // 设置与数据库相关的路由，使用传递进来的数据库引用
        router.setupDatabaseRoutes(db);
    }

private:
    // 成员变量：服务器套接字文件描述符、epoll实例的文件描述符、监听端口号、epoll最大监听事件数
    int server_fd, epollfd, port, max_events;
    // Router对象用于处理HTTP请求的路由分发
    Router router;

    // 数据库引用，用于访问和操作数据库
    Database& db;

    // 设置服务器套接字的方法，包括创建套接字、配置地址信息、设置重用地址选项、绑定端口、监听连接
    void setupServerSocket() {
        // 创建TCP套接字
        server_fd = socket(AF_INET, SOCK_STREAM, 0);

        // 初始化服务器地址结构体
        struct sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        // 设置SO_REUSEADDR选项，允许快速重启服务器并重用相同端口
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定服务器地址到套接字上
        bind(server_fd, (struct sockaddr *)&address, sizeof(address));

        // 开始监听连接请求
        listen(server_fd, SOMAXCONN);

        // 设置服务器套接字为非阻塞模式
        setNonBlocking(server_fd);
    }

    // 设置并初始化epoll实例的方法
    void setupEpoll() {
        // 创建epoll实例
        epollfd = epoll_create1(0);

        // 初始化epoll_event结构体，注册对服务器套接字的EPOLLIN | EPOLLET事件监听
        struct epoll_event event = {};
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event);
    }

    // 接收新连接的方法，将连接放入epoll监听列表中
    void acceptConnection() {
        // 循环接收新的客户端连接请求
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_sock;
        while ((client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_addrlen)) > 0) {
            // 将新接受的客户端套接字设置为非阻塞模式
            setNonBlocking(client_sock);

            // 注册客户端套接字到epoll监听列表，监听EPOLLIN | EPOLLET事件
            struct epoll_event event = {};
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = client_sock;
            epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event);
        }

        // 如果accept失败且错误原因不是EAGAIN或EWOULDBLOCK，则打印错误信息
        if (client_sock == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            LOG_ERROR("Error accepting new connection");
        }
    }

    // 处理客户端连接请求的方法，读取请求、路由分发、生成响应并发送回客户端
    void handleConnection(int fd) {
        char buffer[4096]; // 缓冲区用于存储从客户端读取的数据
        ssize_t bytes_read; // 读取的字节数

        // 循环读取客户端请求数据，直到无数据可读
        while ((bytes_read = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0'; // 在缓冲区末尾添加结束符便于处理

            // 解析请求数据并创建HttpRequest对象
            HttpRequest request;
            if (request.parse(buffer)) {
                // 根据HttpRequest对象通过Router对象获取对应的HttpResponse对象
                HttpResponse response = router.routeRequest(request);

                // 将HttpResponse对象转换为字符串形式，并发送给客户端
                std::string response_str = response.toString();
                send(fd, response_str.c_str(), response_str.length(), 0);
            }
        }

        // 如果读取错误且错误原因不是EAGAIN，则打印错误信息
        if (bytes_read == -1 && errno != EAGAIN) {
            LOG_ERROR("Error reading from socket %d", fd);
        }

        // 处理完请求后关闭客户端连接
        close(fd);
    }

    // 设置文件描述符为非阻塞模式的方法
    void setNonBlocking(int sock) {
        // 获取当前文件描述符的标志位
        int flags = fcntl(sock, F_GETFL, 0);

        // 设置O_NONBLOCK标志位
        flags |= O_NONBLOCK;

        // 更新文件描述符的标志位
        fcntl(sock, F_SETFL, flags);
    }
};