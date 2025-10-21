#pragma once  // 防止头文件被重复包含，避免编译错误

#include <sys/socket.h>    // socket、bind、listen、accept 等
#include <sys/event.h>     // kqueue、kevent (Mac/BSD 的 I/O 多路复用)
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
 * HttpServer: 使用 kqueue + 线程池 的轻量级 HTTP 服务器（Mac 版本）
 * - 非阻塞 socket
 * - 短连接 (处理完即关闭)
 * - 使用 kqueue 替代 Linux 的 epoll
 */
class HttpServer {
public:
    /**
     * 构造函数
     * @param port       监听端口
     * @param max_events kqueue 最多同时处理的事件数
     * @param db         引用外部 Database 实例，用于用户注册、登录等持久化
     */
    HttpServer(int port, int max_events, Database& db)
        : server_fd(-1), kq(-1),
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
        // POST /infer 接收 JSON {"user_message": "...", "chat_id":"...", "max_tokens":..., "temperature":...}
        // 也兼容旧格式 {"prompt": "..."}
        router.addRoute("POST", "/infer", [](const HttpRequest& req) {
            try {
                std::cerr << "[/infer] Request received, body length: " << req.getBody().size() << std::endl;

                // 使用通用JSON解析器（支持字符串和数字）
                auto json = req.parseJson();
                std::cerr << "[/infer] JSON parsed, fields count: " << json.size() << std::endl;

                // 优先使用 user_message，如果没有则用 prompt（向后兼容）
                std::string userMsg;
                if (json.count("user_message")) {
                    userMsg = json["user_message"];
                } else if (json.count("prompt")) {
                    userMsg = json["prompt"];
                }

                if (userMsg.empty()) {
                    return HttpResponse::makeErrorResponse(400, "user_message or prompt required");
                }

                std::string chatId = "default";
                if (json.count("chat_id") && !json["chat_id"].empty()) {
                    chatId = json["chat_id"];
                }

                // 解析可选参数（如果不存在则使用默认值）
                int maxTokens = 64;
                float temperature = 0.7f;

                if (json.count("max_tokens") && !json["max_tokens"].empty()) {
                    std::string maxTokStr = json["max_tokens"];
                    std::cerr << "[/infer] max_tokens raw value: [" << maxTokStr << "] len=" << maxTokStr.size() << std::endl;
                    try {
                        maxTokens = std::stoi(maxTokStr);
                        std::cerr << "[/infer] max_tokens parsed: " << maxTokens << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[/infer] Failed to parse max_tokens: [" << maxTokStr << "] error: " << e.what() << std::endl;
                        maxTokens = 64;
                    }
                }

                if (json.count("temperature") && !json["temperature"].empty()) {
                    std::string tempStr = json["temperature"];
                    std::cerr << "[/infer] temperature raw value: [" << tempStr << "] len=" << tempStr.size() << std::endl;
                    try {
                        temperature = std::stof(tempStr);
                        std::cerr << "[/infer] temperature parsed: " << temperature << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "[/infer] Failed to parse temperature: [" << tempStr << "] error: " << e.what() << std::endl;
                        temperature = 0.7f;
                    }
                }

                // 输出调试信息
                std::cerr << "[/infer] Final parameters:" << std::endl;
                std::cerr << "  chat_id: " << chatId << std::endl;
                std::cerr << "  user_message: " << userMsg.substr(0, std::min(size_t(50), userMsg.size())) << "..." << std::endl;
                std::cerr << "  max_tokens: " << maxTokens << std::endl;
                std::cerr << "  temperature: " << temperature << std::endl;

                // 单例模型管理器，用 chatId 区分会话
                auto& mgr = ModelManager::instance();
                std::cerr << "[/infer] Calling ModelManager::infer..." << std::endl;
                std::string answer = mgr.infer(chatId, userMsg, maxTokens, temperature);
                std::cerr << "[/infer] Inference completed, answer length: " << answer.size() << std::endl;

                HttpResponse r(200);
                r.setHeader("Content-Type", "application/json");
                r.setBody("{\"answer\":\"" + answer + "\"}");
                return r;

            } catch (const std::exception& e) {
                std::cerr << "[/infer] EXCEPTION: " << e.what() << std::endl;
                return HttpResponse::makeErrorResponse(500, std::string("error: ") + e.what());
            } catch (...) {
                std::cerr << "[/infer] EXCEPTION (unknown)" << std::endl;
                return HttpResponse::makeErrorResponse(500, "unknown error");
            }
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
     * - 创建 kqueue，注册 server_fd
     * - 注册所有路由
     * - 循环 kevent，分发 accept 或将连接提交给线程池处理
     */
    void start() {
        setupServerSocket();
        setupKqueue();

        // 1) 注册 HTTP 路由
        setupRoutes();
        setupInferRoute();
        // 注意：setupChatRoutes 也包含一个旧的 /infer 路由，会覆盖 setupInferRoute
        // 所以这里不调用 setupChatRoutes，只使用新的 /infer 实现
        // router.setupChatRoutes(db, ModelManager::instance());
        router.setupStaticPages();

        // 2) 创建固定大小线程池
        ThreadPool pool(/*threads=*/4, /*queue_size=*/16);
        // 3) kqueue 事件数组
        std::vector<struct kevent> events(max_events);

        // 4) 主循环
        while (true) {
            int nfds = kevent(kq, nullptr, 0, events.data(), max_events, nullptr);
            if (nfds < 0) {
                LOG_ERROR("kevent failed: %s", strerror(errno));
                continue;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = static_cast<int>(events[i].ident);
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
    int kq;                      // kqueue fd
    int port;                    // 监听端口
    int max_events;              // kqueue 最大事件数
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

    /** 创建 kqueue 并注册 server_fd */
    void setupKqueue() {
        kq = kqueue();
        if (kq < 0) {
            LOG_ERROR("kqueue creation failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        struct kevent ev;
        // 监听 server_fd 的可读事件
        EV_SET(&ev, server_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
        if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
            LOG_ERROR("kevent ADD server_fd failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    /** 接受新连接并注册到 kqueue */
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
            // 将新连接加入 kqueue 监听
            struct kevent ev;
            EV_SET(&ev, client_sock, EVFILT_READ, EV_ADD, 0, 0, nullptr);
            if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
                LOG_ERROR("kevent ADD client_sock failed: %s", strerror(errno));
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

        // 如果需要关闭，从 kqueue 删除监听并 close fd
        if (closed) {
            struct kevent ev;
            EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            kevent(kq, &ev, 1, nullptr, 0, nullptr);
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
