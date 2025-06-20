#pragma once

#include <sys/socket.h>
#include <sys/epoll.h>
#include <iostream>
#include <fcntl.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "Logger.h"
#include "ThreadPool.h"
#include "Router.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "inference/ModelManager.h"

class HttpServer {
public:
    HttpServer(int port, int max_events, Database& db)
        : server_fd(-1), epollfd(-1), port(port),
          max_events(max_events), db(db) {}

    // 注册 GET "/" 及 /register、/login
    void setupRoutes() {
        router.addRoute("GET", "/", [](const HttpRequest& req) {
            HttpResponse resp;
            resp.setStatusCode(200);
            resp.setBody("Hello, World!");
            return resp;
        });
        router.setupDatabaseRoutes(db);
    }

    // 注册 POST "/infer"
void setupInferRoute() {
    router.addRoute("POST", "/infer", [](const HttpRequest& req) {
        using clk = std::chrono::steady_clock;
        auto t0 = clk::now();

        std::string userPrompt = req.parseJsonField("prompt");
        if (userPrompt.empty()) {
            return HttpResponse::makeErrorResponse(400, "Bad Request: missing prompt");
        }
        std::cerr << "[Router] raw prompt = " << userPrompt << std::endl;

        // 注意分隔符精确！
        std::string fullPrompt =
            "<|system|>\nYou are a helpful assistant.</s>\n"
            "<|user|>\n" + userPrompt + "</s>\n"
            "<|assistant|>";

        std::cerr << "[Router] full prompt = " << fullPrompt << std::endl;

        auto& mgr = ModelManager::instance();
        auto t1 = clk::now();
        std::cerr << "[Infer] >> llama start\n";

        // 新增：捕获异常，检查 tokenize 结果
        std::string output;
        try {
            output = mgr.infer(fullPrompt, /*maxTokens=*/32, /*temperature=*/0.7f);
        } catch (const std::exception& e) {
            std::cerr << "[Infer]  !! Exception: " << e.what() << std::endl;
            return HttpResponse::makeErrorResponse(500, "Model inference error");
        }
        if (output == "[tokenize_failed]") {
            std::cerr << "[Infer]  !! Tokenize failed\n";
            return HttpResponse::makeErrorResponse(400, "Prompt format error");
        }

        auto t2 = clk::now();
        std::cerr << "[Infer] << llama done, elapsed "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
                  << " ms\n";

        HttpResponse resp;
        resp.setStatusCode(200);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody("{\"result\":\"" + output + "\"}");

        auto t3 = clk::now();
        std::cerr << "[Router] build resp took "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count()
                  << " ms\n";

        return resp;
    });
}


    // 启动服务器：创建 socket、epoll，进入循环
    void start() {
        setupServerSocket();
        setupEpoll();
        ThreadPool pool(4, 16);
        struct epoll_event events[max_events];

        while (true) {
            int nfds = epoll_wait(epollfd, events, max_events, -1);
            if (nfds < 0) {
                LOG_ERROR("epoll_wait error: %s", strerror(errno));
                continue;
            }
            for (int i = 0; i < nfds; ++i) {
                int fd = events[i].data.fd;
                if (fd == server_fd) {
                    acceptConnection();
                } else {
                    pool.enqueue([this, fd]() {
                        this->handleConnection(fd);
                    });
                }
            }
        }
    }

private:
    int server_fd;
    int epollfd;
    int port;
    int max_events;
    Router router;
    Database& db;

    std::unordered_map<int, std::string> recvBuffers;
    std::mutex recvBuffersMutex;

    void setupServerSocket() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            LOG_ERROR("socket creation failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(port);
        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG_ERROR("bind failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (listen(server_fd, SOMAXCONN) < 0) {
            LOG_ERROR("listen failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        setNonBlocking(server_fd);
    }

    void setupEpoll() {
        epollfd = epoll_create1(0);
        if (epollfd < 0) {
            LOG_ERROR("epoll_create1 failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        struct epoll_event event = {};
        event.events   = EPOLLIN | EPOLLET;
        event.data.fd  = server_fd;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &event) < 0) {
            LOG_ERROR("epoll_ctl ADD server_fd failed: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    void acceptConnection() {
        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_sock < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                LOG_ERROR("accept error: %s", strerror(errno));
                break;
            }
            setNonBlocking(client_sock);
            struct epoll_event event = {};
            event.events   = EPOLLIN | EPOLLET;
            event.data.fd  = client_sock;
            if (epoll_ctl(epollfd, EPOLL_CTL_ADD, client_sock, &event) < 0) {
                LOG_ERROR("epoll_ctl ADD client_sock failed: %s", strerror(errno));
                close(client_sock);
            }
        }
    }
void handleConnection(int fd) {
    bool closed = false;

    while (true) {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_ERROR("read error on fd %d: %s", fd, strerror(errno));
            closed = true;
            break;
        }
        if (n == 0) {                  // peer closed
            closed = true;
            break;
        }

        {   // 把新读数据 append 到 per-fd 缓冲
            std::lock_guard lk(recvBuffersMutex);
            recvBuffers[fd].append(buf, n);
        }

        // ---------- 判断是否收全一整个 HTTP 报文 ----------
        bool requestReady = false;
        std::string reqStr;
        {
            std::lock_guard lk(recvBuffersMutex);
            auto &data = recvBuffers[fd];

            // 1) 必须至少有头
            size_t headerEnd = data.find("\r\n\r\n");
            if (headerEnd == std::string::npos) continue;   // 头还没收全

            // 2) 如果是 POST，要看 Content-Length
            //    这里直接手动找，避免 parse() 失败再 400
            size_t bodyLen = 0;
            size_t posCL = data.find("Content-Length:");
            if (posCL != std::string::npos) {
                posCL += 15;                     // 跳过 "Content-Length:"
                while (posCL < data.size() && data[posCL] == ' ') ++posCL;
                bodyLen = std::stoul(data.substr(posCL));
            }
            size_t totalNeeded = headerEnd + 4 + bodyLen;
            if (data.size() < totalNeeded) continue;        // body 还没收全

            // 到这里，头+体都到齐
            requestReady = true;
            reqStr.swap(data);           // 把完整报文拿出来
        }
        // ---------- 解析 ----------
        if (requestReady) {
            HttpRequest request;
            if (!request.parse(reqStr)) {
                HttpResponse bad = HttpResponse::makeErrorResponse(400, "Bad Request");
                std::string respStr = bad.toString();
                send(fd, respStr.c_str(), respStr.size(), 0);
                closed = true;
                break;
            }

            HttpResponse resp = router.routeRequest(request);
            std::string respStr = resp.toString();
            send(fd, respStr.c_str(), respStr.size(), 0);
            closed = true;               // 短连接：一次请求完即关
            break;
        }
    }

    if (closed) {
        epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        std::lock_guard lk(recvBuffersMutex);
        recvBuffers.erase(fd);
    }
}

    void setNonBlocking(int sock) {
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0) return;
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
};
