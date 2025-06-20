#pragma once  // 防止头文件被重复包含

#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "inference/ModelManager.h"

/*==========================================================
 * Extremely simple router (exact match + /chat/{id})
 * 只有精确匹配和一个简单的动态 /chat/{id} 路由
 *=========================================================*/
class Router {
public:
    // 每个路由处理函数签名：接收 HttpRequest，返回 HttpResponse
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    /**
     * 注册一个路由
     * @param m  HTTP 方法，如 "GET","POST"
     * @param p  请求路径，如 "/login"
     * @param h  处理函数
     */
    void addRoute(const std::string& m, const std::string& p, HandlerFunc h) {
        // 用 "METHOD|PATH" 作为唯一 key
        routes[m + "|" + p] = std::move(h);
    }

    /**
     * 路由分发中心：根据请求 method+path 调用对应 handler
     * 支持：
     *  1) 事先注册的精确匹配
     *  2) GET /chat/{id} 的动态 ID 路由
     *  3) 未命中返回 404
     */
    HttpResponse routeRequest(const HttpRequest& req) {
        // 1) 精确匹配
        std::string key = req.getMethodString() + "|" + req.getPath();
        if (auto it = routes.find(key); it != routes.end()) {
            return it->second(req);
        }

        // 2) 动态 /chat/{id}，仅限 GET
        if (req.getMethodString() == "GET" &&
            req.getPath().rfind("/chat/", 0) == 0 &&  // 以 "/chat/" 开头
            chatByIdHandler) {
            return chatByIdHandler(req);
        }

        // 3) 都不匹配
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    /*-------------------------
     * 模块化路由注册函数
     *-------------------------*/

    /// 注册用户注册/登录相关路由
    void setupDatabaseRoutes(Database& db);

    /// 注册聊天相关路由，包括 /chat, /chat/{id}, /infer
    void setupChatRoutes(Database& db, ModelManager& mm);

    /// 注册静态页面路由
    void setupStaticPages();

    /// 添加单个静态文件路由
    void addStatic(const std::string& url, const std::string& file);

private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 保存所有精确路由
    HandlerFunc chatByIdHandler;                         // 专门处理 /chat/{id}
};

/*==================== 实现部分 ====================*/

inline void Router::setupDatabaseRoutes(Database& db) {
    /* --- POST /register --- */
    addRoute("POST", "/register", [&db](const HttpRequest& r) {
        auto p = r.parseFormBody(); // form-urlencoded 解析 username/password
        if (p["username"].empty() || p["password"].empty())
            return HttpResponse::makeErrorResponse(400, "missing field");
        // 调用数据库接口尝试注册
        bool ok = db.registerUser(p["username"], p["password"]);
        return ok
            ? HttpResponse::makeOkResponse("OK")
            : HttpResponse::makeErrorResponse(400, "user exists");
    });

    /* --- POST /login --- */
    addRoute("POST", "/login", [&db](const HttpRequest& r) {
        auto p = r.parseFormBody(); // form-urlencoded 解析
        if (p["username"].empty() || p["password"].empty())
            return HttpResponse::makeErrorResponse(400, "missing field");
        // 验证用户名密码
        bool ok = db.loginUser(p["username"], p["password"]);
        return ok
            ? HttpResponse::makeOkResponse("OK")
            : HttpResponse::makeErrorResponse(403, "bad credential");
    });
}

inline void Router::setupChatRoutes(Database& db, ModelManager& mm) {
    /* --- POST /chat --- 创建新聊天会话 --- */
    addRoute("POST", "/chat", [&db](const HttpRequest& r) {
        int cid = db.createChat(r.parseFormBody()["user"]);
        if (cid < 0) {
            return HttpResponse::makeErrorResponse(500, "create failed");
        }
        // 返回新 chat id
        HttpResponse resp(200);
        resp.setBody(std::to_string(cid));
        return resp;
    });

    /* --- GET /chat?user=xxx --- 列出该用户的所有 chat id --- */
    addRoute("GET", "/chat", [&db](const HttpRequest& r) {
        // 从 query-string 中获取 user 参数
        auto arr = db.listChats(r.getQuery().at("user"));
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < arr.size(); ++i) {
            if (i) oss << ",";
            oss << arr[i];
        }
        oss << "]";
        HttpResponse resp(200);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody(oss.str());
        return resp;
    });

    /* --- GET /chat/{id} --- 获取单个会话的消息列表 --- */
    chatByIdHandler = [&db](const HttpRequest& r) {
        // 从路径中截取数字部分
        int cid = std::stoi(r.getPath().substr(6)); // skip "/chat/"
        auto msgs = db.getMessages(cid);
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < msgs.size(); ++i) {
            if (i) oss << ",";
            // 每条消息: {"role":"user","content":"..."}
            oss << "{\"role\":\"" << msgs[i].first
                << "\",\"content\":\"" << msgs[i].second << "\"}";
        }
        oss << "]";
        HttpResponse resp(200);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody(oss.str());
        return resp;
    };

    /* --- POST /infer --- 聊天推理接口 --- */
    addRoute("POST", "/infer", [&db, &mm](const HttpRequest& r) {
        try {
            // 解析整个 JSON body 为 map
            auto js = r.parseJson();
            if (js.empty()) {
                return HttpResponse::makeErrorResponse(400, "json parse fail");
            }
            int cid              = std::stoi(js["chat_id"]);
            std::string user     = js["user"];
            std::string prompt   = js["prompt"];
            if (prompt.empty()) {
                return HttpResponse::makeErrorResponse(400, "no prompt");
            }
            // 存储用户提问
            db.addMessage(cid, "user", prompt);
            // 调用模型生成回答
            std::string ans = mm.infer(user + "-" + std::to_string(cid),
                                       prompt, 64, 0.7f);
            db.addMessage(cid, "assistant", ans);
            // 返回 JSON 结果
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"answer\":\"" + ans + "\"}");
            return resp;
        }
        catch (const std::exception& e) {
            std::cerr << "[/infer] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, "server error");
        }
    });
}

inline void Router::addStatic(const std::string& url, const std::string& file) {
    // 直接把本地文件内容以 GET 返回
    addRoute("GET", url, [file](const HttpRequest&) {
        return HttpResponse::makeFileResponse(file);
    });
}

inline void Router::setupStaticPages() {
    // 前端静态资源
    addStatic("/login.html",    "../UI/login.html");
    addStatic("/register.html", "../UI/register.html");
    addStatic("/chat.html",     "../UI/chat.html");
    addStatic("/style.css",     "../UI/style.css");
    addStatic("/simple-api.js", "../UI/simple-api.js");

    // 根路径重定向到登录页面
    addRoute("GET", "/", [](const HttpRequest&) {
        HttpResponse r(302);
        r.setHeader("Location", "/login.html");
        return r;
    });
}
