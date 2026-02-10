#pragma once  // 防止头文件被重复包含

#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <algorithm>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "inference/ModelManager.h"
#include "SessionManager.h"  // Server-12新增：会话管理

/*==========================================================
 * Router - Server-12 多轮对话路由器
 *
 * 新增功能：
 * - 支持多会话管理（类似ChatGPT的多对话窗口）
 * - 支持会话历史记录
 * - 支持新增/删除会话
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
     *  1) 精确匹配
     *  2) 动态路由匹配（/chat/{id}, /api/sessions/{id}/*）
     *  3) 未命中返回 404
     */
    HttpResponse routeRequest(const HttpRequest& req) {
        // 1) 精确匹配
        std::string key = req.getMethodString() + "|" + req.getPath();
        if (auto it = routes.find(key); it != routes.end()) {
            return it->second(req);
        }

        // 2) 动态路由匹配
        std::string path = req.getPath();
        std::string method = req.getMethodString();

        // /chat/{id} (旧版兼容)
        if (method == "GET" && path.rfind("/chat/", 0) == 0 && chatByIdHandler) {
            return chatByIdHandler(req);
        }

        // /api/sessions/{session_id}/history
        if (method == "GET" && path.rfind("/api/sessions/", 0) == 0 &&
            path.find("/history") != std::string::npos && sessionHistoryHandler) {
            return sessionHistoryHandler(req);
        }

        // /api/sessions/{session_id}/chat
        if (method == "POST" && path.rfind("/api/sessions/", 0) == 0 &&
            path.find("/chat") != std::string::npos && sessionChatHandler) {
            return sessionChatHandler(req);
        }

        // /api/sessions/{session_id} DELETE
        if (method == "DELETE" && path.rfind("/api/sessions/", 0) == 0 && sessionDeleteHandler) {
            return sessionDeleteHandler(req);
        }

        // /api/sessions/{session_id}/title PUT
        if (method == "PUT" && path.rfind("/api/sessions/", 0) == 0 &&
            path.find("/title") != std::string::npos && sessionTitleHandler) {
            return sessionTitleHandler(req);
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

    /// 【Server-12新增】注册会话管理路由
    void setupSessionRoutes(SessionManager& sm, ModelManager& mm);

    /// 注册静态页面路由
    void setupStaticPages();

    /// 添加单个静态文件路由
    void addStatic(const std::string& url, const std::string& file);

private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 保存所有精确路由

    // 动态路由处理器
    HandlerFunc chatByIdHandler;              // /chat/{id}
    HandlerFunc sessionHistoryHandler;        // /api/sessions/{id}/history
    HandlerFunc sessionChatHandler;           // /api/sessions/{id}/chat
    HandlerFunc sessionDeleteHandler;         // /api/sessions/{id}
    HandlerFunc sessionTitleHandler;          // /api/sessions/{id}/title
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

            // 支持字符串形式的chat_id
            std::string chatId = js["chat_id"];
            if (chatId.empty()) chatId = "default";

            std::string user     = js["user"];
            std::string prompt   = js["prompt"];
            if (prompt.empty()) {
                return HttpResponse::makeErrorResponse(400, "no prompt");
            }

            // 解析max_tokens和temperature（带默认值）
            int maxTokens = 64;
            float temperature = 0.7f;
            if (!js["max_tokens"].empty()) {
                try {
                    maxTokens = std::stoi(js["max_tokens"]);
                } catch (...) { }
            }
            if (!js["temperature"].empty()) {
                try {
                    temperature = std::stof(js["temperature"]);
                } catch (...) { }
            }

            // 存储用户提问（仅当chat_id是数字时存入数据库）
            try {
                int cid = std::stoi(chatId);
                db.addMessage(cid, "user", prompt);
            } catch (...) {
                // chat_id不是数字，跳过数据库存储
            }

            // 调用模型生成回答
            std::string sessionKey = user.empty() ? chatId : (user + "-" + chatId);
            std::string ans = mm.infer(sessionKey, prompt, maxTokens, temperature);

            // 存储助手回复（仅当chat_id是数字时）
            try {
                int cid = std::stoi(chatId);
                db.addMessage(cid, "assistant", ans);
            } catch (...) {
                // chat_id不是数字，跳过数据库存储
            }

            // 返回 JSON 结果（统一使用"response"字段）
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"response\":\"" + ans + "\"}");
            return resp;
        }
        catch (const std::exception& e) {
            std::cerr << "[/infer] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, "server error");
        }
    });
}

// JSON转义辅助函数
inline std::string escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c;
        }
    }
    return result;
}

/*==========================================================
 * Server-12 新增：会话管理路由
 *=========================================================*/
inline void Router::setupSessionRoutes(SessionManager& sm, ModelManager& mm) {
    /* --- POST /api/sessions/new --- 创建新会话 --- */
    addRoute("POST", "/api/sessions/new", [&sm](const HttpRequest& r) {
        try {
            std::string session_id = sm.createSession();

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"session_id\":\"" + session_id + "\",\"status\":\"created\"}");
            return resp;
        }
        catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("create session failed: ") + e.what());
        }
    });

    /* --- GET /api/sessions --- 获取所有会话列表 --- */
    addRoute("GET", "/api/sessions", [&sm](const HttpRequest& r) {
        try {
            std::string sessions_json = sm.getAllSessions();

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody(sessions_json);
            return resp;
        }
        catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get sessions failed: ") + e.what());
        }
    });

    /* --- GET /api/sessions/{session_id}/history --- 获取会话历史 --- */
    sessionHistoryHandler = [&sm](const HttpRequest& r) {
        try {
            // 从路径中提取session_id: /api/sessions/{session_id}/history
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;  // "/api/sessions/" 长度14
            size_t end = path.find("/history");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            std::string history_json = sm.getSessionHistory(session_id);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody(history_json);
            return resp;
        }
        catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get history failed: ") + e.what());
        }
    };

    /* --- POST /api/sessions/{session_id}/chat --- 在会话中发送消息 --- */
    sessionChatHandler = [&sm, &mm](const HttpRequest& r) {
        try {
            // 从路径中提取session_id
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/chat");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // 解析请求体
            auto js = r.parseJson();
            std::string user_message = js["message"];
            if (user_message.empty()) {
                return HttpResponse::makeErrorResponse(400, "message is required");
            }

            // 解析参数
            int maxTokens = 100;
            float temperature = 0.7f;
            if (!js["max_tokens"].empty()) {
                try { maxTokens = std::stoi(js["max_tokens"]); } catch (...) { }
            }
            if (!js["temperature"].empty()) {
                try { temperature = std::stof(js["temperature"]); } catch (...) { }
            }

            // 1. 调用 ModelManager 的多轮对话接口，它会处理所有事情
            //    - 添加用户消息
            //    - 调用 getFullContext() (我们已修改为使用模板)
            //    - 执行推理
            //    - 添加助手消息
            std::string assistant_reply = mm.infer(session_id, user_message, maxTokens, temperature);

            // 2. 返回结果
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            std::ostringstream json_resp;
            json_resp << "{"
                      << "\"session_id\":\"" << session_id << "\","
                      << "\"message\":\"" << escapeJson(assistant_reply) << "\","
                      << "\"message_count\":" << sm.getMessageCount(session_id)
                      << "}";
            resp.setBody(json_resp.str());
            return resp;
        }
        catch (const std::exception& e) {
            std::cerr << "[/api/sessions/{id}/chat] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, std::string("chat failed: ") + e.what());
        }
    };

    /* --- DELETE /api/sessions/{session_id} --- 删除会话 --- */
    sessionDeleteHandler = [&sm](const HttpRequest& r) {
        try {
            // 从路径中提取session_id
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            std::string session_id = path.substr(start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            sm.deleteSession(session_id);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"status\":\"deleted\",\"session_id\":\"" + session_id + "\"}");
            return resp;
        }
        catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("delete session failed: ") + e.what());
        }
    };

    /* --- PUT /api/sessions/{session_id}/title --- 更新会话标题 --- */
    sessionTitleHandler = [&sm](const HttpRequest& r) {
        try {
            // 从路径中提取session_id
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/title");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            auto js = r.parseJson();
            std::string new_title = js["title"];
            if (new_title.empty()) {
                return HttpResponse::makeErrorResponse(400, "title is required");
            }

            sm.updateSessionTitle(session_id, new_title);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"status\":\"updated\",\"session_id\":\"" + session_id + "\",\"title\":\"" + new_title + "\"}");
            return resp;
        }
        catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("update title failed: ") + e.what());
        }
    };
}

inline void Router::addStatic(const std::string& url, const std::string& file) {
    // 直接把本地文件内容以 GET 返回
    addRoute("GET", url, [file](const HttpRequest&) {
        return HttpResponse::makeFileResponse(file);
    });
}

inline void Router::setupStaticPages() {
    // 前端静态资源（Docker环境使用UI/，本地环境使用../UI/）
    const char* ui_path = "UI/";  // Docker: /app/UI/, 本地: ./UI/
    addStatic("/login.html",    std::string(ui_path) + "login.html");
    addStatic("/register.html", std::string(ui_path) + "register.html");
    addStatic("/chat.html",     std::string(ui_path) + "chat.html");
    addStatic("/multichat.html", std::string(ui_path) + "multichat.html");  // Server-12新增：多对话界面
    addStatic("/style.css",     std::string(ui_path) + "style.css");
    addStatic("/multichat.css", std::string(ui_path) + "multichat.css");    // Server-12新增：多对话样式
    addStatic("/simple-api.js", std::string(ui_path) + "simple-api.js");

    // 根路径重定向到多对话界面
    addRoute("GET", "/", [](const HttpRequest&) {
        HttpResponse r(302);
        r.setHeader("Location", "/multichat.html");  // Server-12：默认进入多对话界面
        return r;
    });
}
