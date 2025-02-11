// 导入所需的头文件
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "Logger.h" // 自定义的日志记录工具
#include "ThreadPool.h" // 线程池，用于处理并发请求
#include "Router.h" // 路由器，用于分发请求到对应的处理函数
#include "HttpRequest.h" // HTTP请求解析
#include "HttpResponse.h" // HTTP响应构造
#include "Database.h" // 数据库操作
#include <sys/epoll.h> // 使用epoll实现高效的事件驱动
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

// 定义HttpServer类
class HttpServer {
public:
    // 构造函数，初始化服务器设置，包括端口、事件最大数量和数据库引用
    HttpServer(int port, int max_events, Database& db)
        : port(port), max_events(max_events), db(db) {
        SSL_library_init(); // 初始化OpenSSL
        OpenSSL_add_ssl_algorithms(); // 加载SSL算法
        SSL_load_error_strings(); // 加载错误提示字符串
        const SSL_METHOD* method = TLS_server_method(); // 设置SSL方法为TLS_server_method
        sslCtx = SSL_CTX_new(method); // 创建SSL上下文
        if (!sslCtx) {
            throw std::runtime_error("Unable to create SSL context");
        }

        // 加载服务器证书和私钥
        if (SSL_CTX_use_certificate_file(sslCtx, "server.crt", SSL_FILETYPE_PEM) <= 0 ||
            SSL_CTX_use_PrivateKey_file(sslCtx, "server.key", SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            throw std::runtime_error("Failed to load cert or key file");
        }
        
        setupRoutes(); // 设置路由
    }

    // 析构函数，释放资源
    ~HttpServer() {
        SSL_CTX_free(sslCtx); // 释放SSL上下文
        EVP_cleanup(); // 清理加密库
    }

    // 启动服务器的主要流程
    void start() {
        setupServerSocket(); // 设置服务器socket
        setupEpoll(); // 设置epoll
        ThreadPool pool(16); // 创建线程池

        std::vector<struct epoll_event> events(max_events); // 事件数组

        // 主循环，不断接收和处理事件
        while (true) {
            int nfds = epoll_wait(epollfd, events.data(), max_events, -1); // 等待事件发生
            for (int i = 0; i < nfds; ++i) {
                if (events[i].data.fd == server_fd) { // 如果是服务器socket事件，接受新连接
                    acceptConnection();
                } else { // 否则处理已有连接的读写事件
                    LOG_INFO("Handling connection for fd: %d", events[i].data.fd);
                    pool.enqueue([this, fd = events[i].data.fd] { handleConnection(fd); });
                }
            }
        }
    }

    // 设置路由函数，添加处理路由
    void setupRoutes() {
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse response;
            response.setStatusCode(200);
            response.setBody("Hello, World!");
            return response;
        });

        // 根据需要添加更多路由
        router.setupDatabaseRoutes(db);
        LOG_INFO("Routes setup completed.");  // 添加日志
    }

private:
    int server_fd = -1, epollfd = -1, port, max_events;
    Router router; // 路由器实例
    Database& db; // 数据库引用
    SSL_CTX* sslCtx; // SSL上下文
    std::map<int, SSL*> sslMap; // 存储每个连接的SSL对象的映射

    // 添加SSL对象到映射中，用于跟踪每个连接的SSL状态。
    void addSSLToMap(int fd, SSL* ssl) {
        sslMap[fd] = ssl; // 将文件描述符与其对应的SSL对象关联。
        LOG_INFO("Added SSL object for fd: %d to map", fd); // 记录日志信息。
    }

    // 从映射中获取指定文件描述符对应的SSL对象。
    SSL* getSSLFromMap(int fd) {
        auto it = sslMap.find(fd); // 查找指定的文件描述符。
        if (it != sslMap.end()) { // 如果找到了对应的条目，
            LOG_INFO("Found SSL object for fd: %d in map", fd); // 记录日志信息。
            return it->second; // 返回找到的SSL对象。
        }
        LOG_ERROR("getSSL object not found for fd: %d in map", fd); // 如果未找到，记录错误日志。
        return nullptr; // 返回空指针。
    }

    // 从映射中移除指定文件描述符对应的SSL对象，并释放相关资源。
    void removeSSLFromMap(int fd) {
        auto it = sslMap.find(fd); // 查找指定的文件描述符。
        if (it != sslMap.end()) { // 如果找到了对应的条目，
            SSL_free(it->second); // 释放对应的SSL对象。
            sslMap.erase(it); // 从映射中移除该条目。
            LOG_INFO("Removed SSL object for fd: %d from map", fd); // 记录日志信息。
        }
    }

    // 初始化服务器的socket，配置监听端口等。
    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0); // 创建TCP socket。
        if (server_fd == -1) {
            throw std::runtime_error("socket failed"); // 如果创建失败，抛出异常。
        }

        // 设置socket地址和端口。
        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY; // 监听任意地址。
        address.sin_port = htons(port); // 监听指定端口。

        int opt = 1;
        // 设置socket选项，允许重用地址。
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定socket到指定的地址和端口。
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
            throw std::runtime_error("bind failed"); // 如果绑定失败，抛出异常。
        }

        // 开始监听端口。
        if (listen(server_fd, SOMAXCONN) == -1) {
            throw std::runtime_error("listen failed"); // 如果监听失败，抛出异常。
        }

        setNonBlocking(server_fd); // 将socket设置为非阻塞模式。
    }

    // 初始化epoll事件监听。
    void setupEpoll() {
        epollfd = epoll_create1(0); // 创建epoll实例。
        if (epollfd == -1) {
            throw std::runtime_error("epoll_create1 failed"); // 如果创建失败，抛出异常。
        }

        struct epoll_event event{};
        event.events = EPOLLIN | EPOLLET; // 监听读事件，边缘触发模式。
        event.data.fd = server_fd; // 关联服务器socket的文件描述符。
        // 将服务器socket添加到epoll监听。
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
            throw std::runtime_error("epoll_ctl failed"); // 如果操作失败，抛出异常。
        }
    }

    // 将新接受的客户端连接添加到epoll监听中，并关联SSL对象。
    void addClientToEpoll(int client_fd, SSL* ssl) {
        struct epoll_event event = {0};
        event.events = EPOLLIN | EPOLLET; // 监听读事件，边缘触发模式。
        event.data.fd = client_fd; // 关联客户端的文件描述符。
        // 尝试将客户端socket添加到epoll监听。
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &event) != 0) {
            LOG_ERROR("Failed to add client socket to epoll"); // 如果操作失败，记录错误日志。
            SSL_free(ssl); // 释放SSL对象。
            close(client_fd); // 关闭客户端连接。
        } else {
            addSSLToMap(client_fd, ssl); // 将SSL对象与客户端连接关联。
            LOG_INFO("Added new client to epoll and ssl map"); // 记录日志信息。
        }
    }

    // 处理HTTP请求，包括解析请求、路由处理、通过SSL发送响应。
    void processRequest(const char* buffer, int fd, SSL* ssl) {
        HttpRequest request; // 创建HTTP请求对象。
        if (!request.parse(buffer)) { // 尝试解析HTTP请求。
            LOG_ERROR("Failed to parse HTTP request"); // 如果解析失败，记录错误日志。
            return; // 结束处理。
        }

        HttpResponse response = router.routeRequest(request); // 根据路由处理请求。
        std::string response_str = response.toString(); // 将响应转换为字符串形式。

        int bytes_sent = SSL_write(ssl, response_str.c_str(), response_str.length()); // 通过SSL发送响应。
        if (bytes_sent <= 0) { // 检查发送结果。
            int err = SSL_get_error(ssl, bytes_sent); // 获取SSL错误代码。
            LOG_ERROR("SSL_write failed with SSL error: %d", err); // 记录SSL发送失败的错误日志。
        } else {
            LOG_INFO("Response sent to client"); // 记录响应发送成功的日志。
        }
    }

    // 处理每个客户端连接，包括读取请求、处理请求、发送响应。
    void handleConnection(int fd) {
        SSL* ssl = getSSLFromMap(fd); // 从映射中获取关联的SSL对象。
        if (!ssl) { // 检查SSL对象是否存在。
            LOG_ERROR("SSL object not found for fd: %d", fd); // 如果不存在，记录错误日志。
            close(fd); // 关闭连接。
            return; // 结束处理。
        }

        char buffer[4096]; // 创建数据缓冲区。
        int bytes_read = SSL_read(ssl, buffer, sizeof(buffer) - 1); // 通过SSL读取数据。

        if (bytes_read > 0) { // 检查读取结果。
            buffer[bytes_read] = '\0'; // 确保字符串以空字符结束。
            processRequest(buffer, fd, ssl); // 处理HTTP请求。
        } else if (bytes_read <= 0) { // 如果读取失败，
            int err = SSL_get_error(ssl, bytes_read); // 获取SSL错误代码。
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) { // 检查是否是非阻塞IO的正常等待状态。
                LOG_INFO("SSL_read needs more data, waiting for next epoll event."); // 记录日志，等待更多数据。
            } else {
                LOG_ERROR("SSL_read failed for fd: %d with SSL error: %d", fd, err); // 记录SSL读取失败的错误日志。
                ERR_print_errors_fp(stderr); // 打印错误信息到标准错误输出。
                removeSSLFromMap(fd); // 从映射中移除SSL对象。
                close(fd); // 关闭连接。
            }
        }
    }

    // 接受新的连接请求
    void acceptConnection() {
        struct sockaddr_in client_addr; // 定义客户端地址结构
        socklen_t client_len = sizeof(client_addr); // 获取地址结构的长度
        int client_fd; // 定义客户端文件描述符

        // 循环接受所有到达的连接请求
        while ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len)) > 0) {
            LOG_INFO("Accepted new connection, fd: %d", client_fd); // 日志记录新连接的文件描述符
            setNonBlocking(client_fd); // 设置新连接为非阻塞模式

            SSL* ssl = SSL_new(sslCtx); // 为新连接创建一个新的SSL对象
            SSL_set_fd(ssl, client_fd); // 将新创建的SSL对象与客户端的文件描述符绑定
            LOG_INFO("SSL object created and set for fd: %d", client_fd); // 记录SSL对象创建和设置的日志

            // 尝试进行非阻塞的SSL握手
            while (true) {
                int ssl_err = SSL_accept(ssl); // 进行SSL握手
                if (ssl_err <= 0) { // 如果SSL握手未完成
                    int err = SSL_get_error(ssl, ssl_err); // 获取SSL错误代码
                    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                        // 如果是因为非阻塞IO而暂时不能继续握手，则需要等待更多的数据
                        struct epoll_event event = {0}; // 定义epoll事件
                        event.events = EPOLLIN | EPOLLET | (err == SSL_ERROR_WANT_WRITE ? EPOLLOUT : 0); // 设置事件类型
                        event.data.ptr = ssl; // 将SSL对象作为事件数据
                        event.data.fd = client_fd; // 将客户端文件描述符作为事件数据
                        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &event) == 0) {
                            addSSLToMap(client_fd, ssl); // 将SSL对象添加到映射中
                            break; // 成功添加到epoll后退出循环
                        } else {
                            LOG_ERROR("Epoll_ctl ADD failed: %s", strerror(errno)); // 记录epoll_ctl失败的日志
                            SSL_free(ssl); // 释放SSL对象
                            close(client_fd); // 关闭客户端连接
                        }
                    } else {
                        ERR_print_errors_fp(stderr); // 打印SSL错误信息
                        SSL_free(ssl); // 释放SSL对象
                        close(client_fd); // 关闭客户端连接
                    }
                    break; // 退出循环
                } else {
                    // 如果SSL握手成功
                    addClientToEpoll(client_fd, ssl); // 将客户端和其SSL对象添加到epoll监控中
                    break; // 退出循环
                }
            }
        }

        if (client_fd == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("Accept failed: %s", strerror(errno)); // 如果接受连接失败，记录错误日志
        }
    }


    // 将文件描述符设置为非阻塞模式
    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0); // 获取当前的文件状态标志
        if (flags == -1) {
            throw std::runtime_error("fcntl F_GETFL failed"); // 获取失败，抛出异常
        }
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) { // 添加非阻塞标志
            throw std::runtime_error("fcntl F_SETFL failed"); // 设置失败，抛出异常
        }
    }
};
