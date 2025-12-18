#pragma once  // 防止头文件被重复包含，避免编译错误

#include <sys/socket.h>    // socket、bind、listen、accept 等
#include <sys/epoll.h>     // epoll_create1、epoll_ctl、epoll_wait
#include <iostream>        // std::cerr
#include <fcntl.h>         // fcntl 用于设置非阻塞
#include <netinet/in.h>    // sockaddr_in, INADDR_ANY, htons
#include <unistd.h>        // read, write, close
#include <cstring>         // strerror, memset
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "Logger.h"        // 日志宏 LOG_ERROR
#include "ThreadPool.h"    // 线程池，用于并发处理连接
#include "Router.h"        // 路由器，将请求分发到不同 handler
#include "HttpRequest.h"   // HTTP 请求解析器
#include "HttpResponse.h"  // HTTP 响应构造器
#include "Database.h"      // 数据库封装
#include "inference/ModelManager.h" // LLM 模型管理

/**
 * HttpServer: 使用 epoll + 线程池 的轻量级 HTTP 服务器
 * - 非阻塞 socket
 * - 边缘触发 (EPOLLET)
 * - 短连接 (处理完即关闭)
 */
class HttpServer {
public:
    /**
     * 构造函数
     * @param port       监听端口
     * @param max_events epoll 最多同时处理的事件数
     * @param db         引用外部 Database 实例，用于用户注册、登录等持久化
     */
    HttpServer(int port, int max_events, Database& db)
        : server_fd(-1), epollfd(-1),
          port(port), max_events(max_events), db(db) {}

    /** 注册静态路由：根 "/", /register, /login 等 */
    void setupRoutes() {
        // GET "/" 返回 Hello, World!
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setBody("Hello, World!");
            return resp;
        });
        // 注册数据库相关路由：/register, /login
        router.setupDatabaseRoutes(db);
    }

    /** 注册推理相关路由：/infer, /reset */
    void setupInferRoute() {
        // POST /infer 接收 JSON {"prompt": "...", "chat_id":"..."}，返回 {"answer":"..."}
        router.addRoute("POST", "/infer", [](const HttpRequest& req) {
            auto prompt  = req.parseJsonField("prompt");
            auto chatId  = req.parseJsonField("chat_id");
            if (prompt.empty()) {
                return HttpResponse::makeErrorResponse(400, "prompt required");
            }
            if (chatId.empty()) chatId = "default";

            // 单例模型管理器，用 chatId 区分会话
            auto& mgr = ModelManager::instance();
            std::string answer = mgr.infer(chatId, prompt, /*maxTokens=*/64, /*temp=*/0.7f);

            HttpResponse r(200);
            r.setHeader("Content-Type", "application/json");
            r.setBody("{\"answer\":\"" + answer + "\"}");
            return r;
        });

        // POST /reset 重置会话 KV cache
        router.addRoute("POST", "/reset", [](const HttpRequest& req) {
            auto id = req.parseJsonField("chat_id");
            if (id.empty()) id = "default";
            ModelManager::instance().dropSession(id);
            return HttpResponse::makeOkResponse("reset ok");
        });
    }

    /**
     * 启动服务器主循环
     * - 创建并配置 server socket
     * - 创建 epoll，注册 server_fd
     * - 注册所有路由
     * - 循环 epoll_wait，分发 accept 或将连接提交给线程池处理
     */
    void start() {
        setupServerSocket();
        setupEpoll();

        // 1) 注册 HTTP 路由
        setupRoutes();
        setupInferRoute();
        router.setupChatRoutes(db, ModelManager::instance());
        router.setupStaticPages();

        // 2) 创建固定大小线程池
        ThreadPool pool(/*threads=*/4, /*queue_size=*/16);
        // 3) epoll 事件数组
        std::vector<struct epoll_event> events(max_events);

        // 4) 主循环
        while (true) {
            int nfds = epoll_wait(epollfd, events.data(), max_events, -1);
            if (nfds < 0) {
                LOG_ERROR("epoll_wait failed: %s", strerror(errno));
                continue;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                if (fd == server_fd) {
                    // 新连接到来
                    acceptConnection();
                } else {
                    // 数据就绪，提交给线程池处理
                    pool.enqueue([this, fd] {
                        handleConnection(fd);
                    });
                }
            }
        }
    }

private:
    int server_fd;               // 监听 socket fd
    int epollfd;                 // epoll fd
    int port;                    // 监听端口
    int max_events;              // epoll 最大事件数
    Router router;               // 路由管理器
    Database& db;                // 数据库引用

    // 每个 fd 的读缓冲，防止半包
    std::unordered_map<int, std::string> recvBuffers;
    std::mutex recvBuffersMutex; // 保护 recvBuffers

    /** 创建 server socket 并监听 */
    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            LOG_ERROR("socket creation failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        // 端口重用
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 绑定到指定端口
        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERROR("bind failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        // 开始监听
        if (listen(server_fd, SOMAXCONN) < 0) {
            LOG_ERROR("listen failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        setNonBlocking(server_fd);
    }

    /** 创建 epoll 并注册 server_fd */
    void setupEpoll() {
        epollfd = epoll_create1(0);
        if (epollfd < 0) {
            LOG_ERROR("epoll_create1 failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        struct epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;  // 可读，边缘触发
        ev.data.fd = server_fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
            LOG_ERROR("epoll_ctl ADD server_fd failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    /** 接受新连接并注册到 epoll */
    void acceptConnection() {
        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_fd,
                                     (struct sockaddr*)&client_addr,
                                     &client_len);
            if (client_sock < 0) {
                // 非阻塞 accept 完成
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                LOG_ERROR("accept error: %s", strerror(errno));
                break;
            }
            setNonBlocking(client_sock);
            // 将新连接加入 epoll 监听
            struct epoll_event ev{};
            ev.events  = EPOLLIN | EPOLLET;
            ev.data.fd = client_sock;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &ev) < 0) {
                LOG_ERROR("epoll_ctl ADD client_sock failed: %s", strerror(errno));
                close(client_sock);
            }
        }
    }

    /**
     * 处理客户端连接：
     * - 读数据到缓冲
     * - 判断是否收到完整 HTTP 请求
     * - 调用 HttpRequest 解析
     * - 交给 Router 生成 HttpResponse
     * - send 并关闭连接（短链接）
     */
    void handleConnection(int fd) {
        bool closed = false;

        while (true) {
            char buf[4096];
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                // 非阻塞无数据
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                LOG_ERROR("read error on fd %d: %s", fd, strerror(errno));
                closed = true;
                break;
            }
            if (n == 0) {
                // 对端关闭
                closed = true;
                break;
            }

            // 将读取到的数据追加到 per-client 缓冲
            {
                std::lock_guard lk(recvBuffersMutex);
                recvBuffers[fd].append(buf, n);
            }

            // 判断是否收到完整请求头 + body
            bool requestReady = false;
            std::string reqStr;
            {
                std::lock_guard lk(recvBuffersMutex);
                auto &data = recvBuffers[fd];

                // 查找 "\r\n\r\n" 分隔头和体
                size_t headerEnd = data.find("\r\n\r\n");
                if (headerEnd == std::string::npos) continue;

                // 如果是 POST，解析 Content-Length
                size_t bodyLen = 0;
                size_t posCL = data.find("Content-Length:");
                if (posCL != std::string::npos) {
                    posCL += strlen("Content-Length:");
                    while (posCL < data.size() && data[posCL]==' ') ++posCL;
                    bodyLen = std::stoul(data.substr(posCL));
                }
                size_t totalNeeded = headerEnd + 4 + bodyLen;
                if (data.size() < totalNeeded) continue;

                // 整个请求已接收完整
                requestReady = true;
                reqStr.swap(data);  // 摆脱缓冲引用，取出完整请求
            }

            if (requestReady) {
                HttpRequest request;
                if (!request.parse(reqStr)) {
                    // 解析失败 => 400
                    auto bad = HttpResponse::makeErrorResponse(400, "Bad Request");
                    std::string respStr = bad.toString();
                    send(fd, respStr.c_str(), respStr.size(), 0);
                    closed = true;
                    break;
                }
                // 路由分发并构造响应
                HttpResponse resp = router.routeRequest(request);
                std::string respStr = resp.toString();
                send(fd, respStr.c_str(), respStr.size(), 0);
                closed = true;  // 短连接：处理完毕就关闭
                break;
            }
        }

        // 如果需要关闭，删除 epoll 监听并 close fd
        if (closed) {
            epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
            std::lock_guard lk(recvBuffersMutex);
            recvBuffers.erase(fd);
        }
    }

    /** 将 socket 设置为非阻塞模式 */
    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0) return;
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
};
