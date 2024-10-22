// HttpServer.h
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
#include <fstream>
#include <sstream>

class HttpServer {
public:
    HttpServer(int port, int max_events, Database& db) : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {}

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

    // 12新增
    std::string readFile(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            return "Error: Unable to open file " + filePath;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    void setupRoutes() {
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });
        router.addRoute("GET", "/login", [this](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/login.html"));
            return response;
        });

        router.addRoute("GET", "/register", [this](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/register.html"));
            return response;
        });

        router.addRoute("GET", "/upload", [this](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "text/html");
            response.setBody(readFile("UI/upload.html")); // 确保upload.html位于UI文件夹中
            return response;
        });

        router.setupDatabaseRoutes(db);
        router.setupImageRoutes(db); 
        // ... 添加更多路由 ...

    }

private:
    int server_fd, epollfd, port, max_events;
    Router router;
    Database& db;

    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        bind(server_fd, (struct sockaddr *)&address, sizeof(address));
        listen(server_fd, SOMAXCONN);

        setNonBlocking(server_fd);
    }

    void setupEpoll() {
        epollfd = epoll_create1(0);
        struct epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event);
    }

    void acceptConnection() {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int client_sock;
        while ((client_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_addrlen)) > 0) {
            setNonBlocking(client_sock);
            struct epoll_event event = {};
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = client_sock;
            epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event);
        }
        if (client_sock == -1 && (errno != EAGAIN && errno != EWOULDBLOCK)) {
            LOG_ERROR("Error accepting new connection");
        }
    }
    void sendBadRequestResponse(int fd) {
        const char* response = 
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: 47\r\n"
            "\r\n"
            "<html><body><h1>400 Bad Request</h1></body></html>";
        send(fd, response, strlen(response), 0);
    }
    
    void handleConnection(int fd) {
        std::string requestBuffer; // 用于存储从客户端接收到的请求数据
        char buffer[4096]; // 临时缓冲区，用于一次性读取数据
        ssize_t bytes_read; // 存储每次调用read函数返回的字节数
        bool requestComplete = false; // 标记请求是否已完全接收

        // 循环读取客户端发送的数据
        while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
            requestBuffer.append(buffer, bytes_read); // 将读取的数据追加到请求缓冲区
            // 检查请求是否包含HTTP请求头和请求体的结束标记"\r\n\r\n"
            if (requestBuffer.find("\r\n\r\n") != std::string::npos) {
                requestComplete = true; // 如果找到，标记请求为完整
                break; // 并退出读取循环
            }
        }

        // 如果读取过程中发生错误，并且错误不是因为非阻塞IO没有数据可读（EAGAIN）
        if (bytes_read == -1 && errno != EAGAIN) {
            LOG_ERROR("Error reading from socket %d: %s", fd, strerror(errno));
            close(fd); // 关闭连接
            return;
        }

        // 如果请求不完整（例如，客户端发送的数据还没完全到达）
        if (!requestComplete) {
            LOG_WARNING("Request not complete for socket %d", fd);
            close(fd); // 关闭连接
            return; // 并退出函数
        }

        // 如果请求完整，尝试解析HTTP请求
        HttpRequest request;
        if (request.parse(requestBuffer)) { // 如果请求成功解析
            HttpResponse response = router.routeRequest(request); // 路由请求，执行相应的处理逻辑
            std::string response_str = response.toString(); // 将响应转换为字符串
            send(fd, response_str.c_str(), response_str.length(), 0); // 发送响应给客户端
        } else { // 如果请求解析失败
            LOG_WARNING("Failed to parse request for socket %d", fd); // 记录警告信息
            // 此处可以增加发送400 Bad Request的逻辑
        }
        close(fd); // 无论成功或失败，处理完请求后关闭连接
    }




    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        flags |= O_NONBLOCK;
        fcntl(sock, F_SETFL, flags);
    }
};
