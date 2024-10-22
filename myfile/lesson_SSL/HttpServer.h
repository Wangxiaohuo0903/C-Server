#pragma once

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "Logger.h"
#include "ThreadPool.h"
#include "Router.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <string>
#include <map>
#include <functional>
#include <memory>
#include <vector>

class HttpServer {
public:
    HttpServer(int port, int max_events, Database& db)
        : port(port), max_events(max_events), db(db) {
        SSL_library_init();
        OpenSSL_add_ssl_algorithms();
        SSL_load_error_strings();
        const SSL_METHOD* method = TLS_server_method(); // 使用TLS_server_method代替SSLv23_server_method
        sslCtx = SSL_CTX_new(method);
        if (!sslCtx) {
            throw std::runtime_error("Unable to create SSL context");
        }

        if (SSL_CTX_use_certificate_file(sslCtx, "server.crt", SSL_FILETYPE_PEM) <= 0 ||
            SSL_CTX_use_PrivateKey_file(sslCtx, "server.key", SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to load cert or key file");
        }
        
        setupRoutes();
    }

    ~HttpServer() {
        SSL_CTX_free(sslCtx);
        EVP_cleanup();
    }

    void start() {
        setupServerSocket();
        setupEpoll();
        ThreadPool pool(16); // 根据需要调整线程池大小

        std::vector<struct epoll_event> events(max_events);

        while (true) {
            int nfds = epoll_wait(epollfd, events.data(), max_events, -1);
            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == server_fd) {
                    acceptConnection();
                } else {
                    LOG_INFO("Handling connection for fd: %d", events[i].data.fd);
                    pool.enqueue([this, fd = events[i].data.fd] { handleConnection(fd); });
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

        // Add more routes as needed
        router.setupDatabaseRoutes(db);
        LOG_INFO("Routes setup completed.");  // 添加日志
    }

private:
    int server_fd = -1, epollfd = -1, port, max_events;
    Router router;
    Database& db;
    SSL_CTX* sslCtx; // SSL context
    std::map<int, SSL*> sslMap;

    void addSSLToMap(int fd, SSL* ssl) {
        sslMap[fd] = ssl;
        LOG_INFO("Added SSL object for fd: %d to map", fd); // 记录日志
    }
    SSL* getSSLFromMap(int fd) {
        auto it = sslMap.find(fd);
        if (it != sslMap.end()) {
            LOG_INFO("Found SSL object for fd: %d in map", fd); // 记录日志
            return it->second;
        }
        LOG_ERROR("getSSL object not found for fd: %d in map", fd); // 记录日志
        return nullptr;
    }

    void removeSSLFromMap(int fd) {
        auto it = sslMap.find(fd);
        if (it != sslMap.end()) {
            SSL_free(it->second);
            sslMap.erase(it);
            LOG_INFO("Removed SSL object for fd: %d from map", fd); // 记录日志
        }
    }
    
    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            throw std::runtime_error("socket failed");
        }

        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
            throw std::runtime_error("bind failed");
        }

        if (listen(server_fd, SOMAXCONN) == -1) {
            throw std::runtime_error("listen failed");
        }

        setNonBlocking(server_fd);
    }

    void setupEpoll() {
        epollfd = epoll_create1(0);
        if (epollfd == -1) {
            throw std::runtime_error("epoll_create1 failed");
        }

        struct epoll_event event{};
        event.events = EPOLLIN | EPOLLET;
        event.data.fd = server_fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            throw std::runtime_error("epoll_ctl failed");
        }
    }
void addClientToEpoll(int client_fd, SSL* ssl) {
    struct epoll_event event = {0};
    event.events = EPOLLIN | EPOLLET; // 监听读事件，边缘触发模式
    event.data.fd = client_fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
        LOG_ERROR("Failed to add client socket to epoll");
        SSL_free(ssl);
        close(client_fd);
    } else {
        addSSLToMap(client_fd, ssl); // 将SSL对象与客户端文件描述符关联
        LOG_INFO("Added new client to epoll and ssl map");
    }
}
void processRequest(const char* buffer, int fd, SSL* ssl) {
    // 解析HTTP请求
    HttpRequest request;
    if (!request.parse(buffer)) {
        LOG_ERROR("Failed to parse HTTP request");
        return;
    }

    // 根据请求路径和方法，找到对应的处理函数
    HttpResponse response = router.routeRequest(request);
    std::string response_str = response.toString();

    // 通过SSL连接发送响应
    int bytes_sent = SSL_write(ssl, response_str.c_str(), response_str.length());
    if (bytes_sent <= 0) {
        int err = SSL_get_error(ssl, bytes_sent);
        LOG_ERROR("SSL_write failed with SSL error: %d", err);
    } else {
        LOG_INFO("Response sent to client");
    }
}

void handleConnection(int fd) {
    SSL* ssl = getSSLFromMap(fd);
    if (!ssl) {
        LOG_ERROR("SSL object not found for fd: %d", fd);
        close(fd);
        return;
    }

    char buffer[4096];
    int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1);

    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        processRequest(buffer, fd, ssl); // 假设processRequest已正确实现
    } else if (bytes_read <= 0) {
        int err = SSL_get_error(ssl, bytes_read);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            LOG_INFO("SSL_read needs more data, waiting for next epoll event.");
        } else {
            LOG_ERROR("SSL_read failed for fd: %d with SSL error: %d", fd, err);
            ERR_print_errors_fp(stderr);
            removeSSLFromMap(fd);
            close(fd);
        }
    }
}



void acceptConnection() {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;

    while ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len)) > 0) {
        LOG_INFO("Accepted new connection, fd: %d", client_fd); // 记录日志
        setNonBlocking(client_fd);

        SSL* ssl = SSL_new(sslCtx);
        SSL_set_fd(ssl, client_fd);
        LOG_INFO("SSL object created and set for fd: %d", client_fd); // 记录日志
        // 非阻塞模式下尝试SSL握手
        while (true) {
            int ssl_err = SSL_accept(ssl);
            if (ssl_err <= 0) {
                int err = SSL_get_error(ssl, ssl_err);
                if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                    // 正确处理非阻塞SSL握手
                    struct epoll_event event = {0};
                    event.events = EPOLLIN | EPOLLET | (err == SSL_ERROR_WANT_WRITE ? EPOLLOUT : 0);
                    event.data.ptr = ssl;  // 存储ssl指针以供后续操作
                    event.data.fd = client_fd; // 使用fd作为事件数据
                    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &event) == 0) {
                        addSSLToMap(client_fd, ssl); // 保存SSL对象以便后续使用
                        break; // 跳出循环，等待epoll事件
                    } else {
                        LOG_ERROR("Epoll_ctl ADD failed: %s", strerror(errno));
                        SSL_free(ssl);
                        close(client_fd);
                    }
                } else {
                    ERR_print_errors_fp(stderr);
                    SSL_free(ssl);
                    close(client_fd);
                }
                break; // 出错或添加到epoll后跳出循环
            } else {
                // SSL握手成功
                addClientToEpoll(client_fd, ssl); // 这里假设addClientToEpoll已正确实现
                break;
            }
        }
    }

    if (client_fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("Accept failed: %s", strerror(errno));
    }
}

    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) {
            throw std::runtime_error("fcntl F_GETFL failed");
        }
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::runtime_error("fcntl F_SETFL failed");
        }
    }
};
