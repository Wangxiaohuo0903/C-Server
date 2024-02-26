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

class HttpServer {
public:
    HttpServer(int port, int max_events, Database& db) 
        : server_fd(-1), epollfd(-1), port(port), max_events(max_events), db(db) {}

    void start() {
        setupServerSocket();
        setupEpoll();
        ThreadPool pool(4,16); // 使用大型线程池处理高并发

        struct epoll_event events[max_events];

        while (true) {
            int nfds = epoll_wait(epollfd, events, max_events, -1);
            for (int n = 0; n < nfds; ++n) {
                if (events[n].data.fd == server_fd) {
                    acceptConnection();
                } else {
                    // 使用线程池异步处理连接
                    pool.enqueue([fd = events[n].data.fd, this]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

    void setupRoutes() {
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });
        router.setupDatabaseRoutes(db);
    }

private:
    int server_fd, epollfd, port, max_events;
    Router router;
    Database& db;

    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in address = {};
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
        struct epoll_event event = {};
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

    void handleConnection(int fd) {
        while (true) {
            std::vector<char> buffer(4096);
            ssize_t bytes_read = read(fd, buffer.data(), buffer.size() - 1);

            // 如果没有数据读取，跳出循环
            if (bytes_read == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_ERROR("Error reading from socket %d", fd);
                }
                break; // 没有更多数据或发生非阻塞读取错误
            } else if (bytes_read == 0) {
                // 对端关闭连接
                break;
            } else {
                buffer[bytes_read] = '\0'; // 确保字符串结束

                // 处理读取到的数据
                HttpRequest request;
                if (request.parse(buffer.data())) {
                    HttpResponse response = router.routeRequest(request);
                    std::string response_str = response.toString();

                    // 尝试一次性发送响应
                    send(fd, response_str.c_str(), response_str.length(), 0);
                }
            }
        }

        // 执行到这里，说明已经完成了本次请求的处理或者客户端关闭了连接
        close(fd); // 关闭连接
    }



    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        flags |= O_NONBLOCK;
        fcntl(sock, F_SETFL, flags);
    }
};
