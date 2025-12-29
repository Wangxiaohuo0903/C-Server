#pragma once

#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <algorithm>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include "ModelManagerV2.h" // CORRECT: Use ModelManagerV2
#include "SessionManager.h"

class Router {
public:
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    void addRoute(const std::string& m, const std::string& p, HandlerFunc h) {
        routes[m + "|" + p] = std::move(h);
    }

    HttpResponse routeRequest(const HttpRequest& req) {
        std::string key = req.getMethodString() + "|" + req.getPath();
        if (auto it = routes.find(key); it != routes.end()) {
            return it->second(req);
        }

        std::string path = req.getPath();
        std::string method = req.getMethodString();

        if (method == "GET" && path.rfind("/chat/", 0) == 0 && chatByIdHandler) {
            return chatByIdHandler(req);
        }
        if (method == "GET" && path.rfind("/api/sessions/", 0) == 0 && path.find("/history") != std::string::npos && sessionHistoryHandler) {
            return sessionHistoryHandler(req);
        }
        // 流式 chat 需要先匹配（更具体的路径）
        if (method == "POST" && path.rfind("/api/sessions/", 0) == 0 && path.find("/chat/stream") != std::string::npos && sessionChatStreamHandler) {
            return sessionChatStreamHandler(req);
        }
        if (method == "POST" && path.rfind("/api/sessions/", 0) == 0 && path.find("/chat") != std::string::npos && sessionChatHandler) {
            return sessionChatHandler(req);
        }
        if (method == "DELETE" && path.rfind("/api/sessions/", 0) == 0 && sessionDeleteHandler) {
            return sessionDeleteHandler(req);
        }
        if (method == "PUT" && path.rfind("/api/sessions/", 0) == 0 && path.find("/title") != std::string::npos && sessionTitleHandler) {
            return sessionTitleHandler(req);
        }

        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    void setupDatabaseRoutes(Database& db);
    void setupChatRoutes(Database& db, ModelManagerV2& mm);
    void setupSessionRoutes(SessionManager& sm, ModelManagerV2& mm); // CORRECT: Use ModelManagerV2
    void setupStaticPages();
    void addStatic(const std::string& url, const std::string& file);

private:
    std::unordered_map<std::string, HandlerFunc> routes;
    HandlerFunc chatByIdHandler;
    HandlerFunc sessionHistoryHandler;
    HandlerFunc sessionChatHandler;
    HandlerFunc sessionChatStreamHandler;  // SSE 流式输出
    HandlerFunc sessionDeleteHandler;
    HandlerFunc sessionTitleHandler;
};

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

inline void Router::setupDatabaseRoutes(Database& db) {
    addRoute("POST", "/register", [&db](const HttpRequest& r) {
        auto p = r.parseFormBody();
        if (p["username"].empty() || p["password"].empty())
            return HttpResponse::makeErrorResponse(400, "missing field");
        bool ok = db.registerUser(p["username"], p["password"]);
        return ok ? HttpResponse::makeOkResponse("OK") : HttpResponse::makeErrorResponse(400, "user exists");
    });
    addRoute("POST", "/login", [&db](const HttpRequest& r) {
        auto p = r.parseFormBody();
        if (p["username"].empty() || p["password"].empty())
            return HttpResponse::makeErrorResponse(400, "missing field");
        bool ok = db.loginUser(p["username"], p["password"]);
        return ok ? HttpResponse::makeOkResponse("OK") : HttpResponse::makeErrorResponse(403, "bad credential");
    });
}

// This function is now mostly legacy, but we keep it for compatibility.
inline void Router::setupChatRoutes(Database& db, ModelManagerV2& mm) {
    addRoute("POST", "/infer", [&db, &mm](const HttpRequest& r) {
        try {
            auto js = r.parseJson();
            std::string chatId = js["chat_id"];
            if (chatId.empty()) chatId = "default";
            std::string prompt = js["prompt"];
            if (prompt.empty()) return HttpResponse::makeErrorResponse(400, "no prompt");

            int maxTokens = js["max_tokens"].empty() ? 100 : std::stoi(js["max_tokens"]);
            float temperature = js["temperature"].empty() ? 0.7f : std::stof(js["temperature"]);

            // Use the new cache-enabled inference
            std::string ans = mm.inferWithCache(chatId, prompt, maxTokens, temperature);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody("{\"response\":\"" + escapeJson(ans) + "\"}");
            return resp;
        } catch (const std::exception& e) {
            std::cerr << "[/infer] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, "server error");
        }
    });
}

// Server-13: Correctly implemented session routes using ModelManagerV2
inline void Router::setupSessionRoutes(SessionManager& sm, ModelManagerV2& mm) {
    addRoute("POST", "/api/sessions/new", [&sm](const HttpRequest& r) {
        try {
            std::string session_id = sm.createSession();
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody("{\"session_id\":\"" + session_id + "\",\"status\":\"created\"}");
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("create session failed: ") + e.what());
        }
    });

    addRoute("GET", "/api/sessions", [&sm](const HttpRequest& r) {
        try {
            std::string sessions_json = sm.getAllSessions();
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody(sessions_json);
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get sessions failed: ") + e.what());
        }
    });

    sessionHistoryHandler = [&sm](const HttpRequest& r) {
        try {
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/history");
            std::string session_id = path.substr(start, end - start);
            if (!sm.hasSession(session_id)) return HttpResponse::makeErrorResponse(404, "session not found");

            std::string history_json = sm.getSessionHistory(session_id);
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody(history_json);
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get history failed: ") + e.what());
        }
    };

    // CORRECT IMPLEMENTATION for Server-13
    sessionChatHandler = [&sm, &mm](const HttpRequest& r) {
        try {
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/chat");
            std::string session_id = path.substr(start, end - start);
            if (!sm.hasSession(session_id)) return HttpResponse::makeErrorResponse(404, "session not found");

            auto js = r.parseJson();
            std::string user_message = js["message"];
            if (user_message.empty()) return HttpResponse::makeErrorResponse(400, "message is required");

            int maxTokens = js["max_tokens"].empty() ? 150 : std::stoi(js["max_tokens"]);
            float temperature = js["temperature"].empty() ? 0.7f : std::stof(js["temperature"]);

            // 1. Add user message to UI session manager
            sm.addUserMessage(session_id, user_message);

            // 2. Get full conversation history to build the prompt
            // Using a large number for turns to get the full history
            std::string full_prompt = sm.getRecentContext(session_id, 20);

            // 3. Call ModelManagerV2 with cache enabled
            std::string assistant_reply = mm.inferWithCache(session_id, full_prompt, maxTokens, temperature);

            // FIX: Trim leading/trailing whitespace from the model's reply
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t\n\r"));
                s.erase(s.find_last_not_of(" \t\n\r") + 1);
            };
            trim(assistant_reply);

            // 4. Add the CLEANED assistant's reply to the UI session manager
            sm.addAssistantMessage(session_id, assistant_reply);

            // 5. Return JSON response
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            std::ostringstream json_resp;
            json_resp << "{"
                      << "\"session_id\":\"" << session_id << "\",\n"
                      << "\"message\":\"" << escapeJson(assistant_reply) << "\",\n"
                      << "\"message_count\":" << sm.getMessageCount(session_id)
                      << "}";
            resp.setBody(json_resp.str());
            return resp;
        } catch (const std::exception& e) {
            std::cerr << "[/api/sessions/{id}/chat] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, std::string("chat failed: ") + e.what());
        }
    };

    // 流式 chat handler - SSE (Server-Sent Events)
    sessionChatStreamHandler = [&sm, &mm](const HttpRequest& r) {
        try {
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/chat/stream");
            std::string session_id = path.substr(start, end - start);
            if (!sm.hasSession(session_id)) return HttpResponse::makeErrorResponse(404, "session not found");

            auto js = r.parseJson();
            std::string user_message = js["message"];
            if (user_message.empty()) return HttpResponse::makeErrorResponse(400, "message is required");

            int maxTokens = js["max_tokens"].empty() ? 150 : std::stoi(js["max_tokens"]);
            float temperature = js["temperature"].empty() ? 0.7f : std::stof(js["temperature"]);

            // 1. Add user message
            sm.addUserMessage(session_id, user_message);

            // 2. Get conversation context
            std::string full_prompt = sm.getRecentContext(session_id, 20);

            // 3. 使用流式输出 - 收集所有 SSE 事件
            std::ostringstream sse_stream;
            std::string full_reply;

            auto callback = [&](const std::string& token) -> bool {
                // 构建 SSE 事件格式
                sse_stream << "data: {\"token\":\"" << escapeJson(token) << "\"}\n\n";
                full_reply += token;
                return true; // 继续生成
            };

            // 调用流式推理
            mm.inferWithCacheStreaming(session_id, full_prompt, maxTokens, temperature, callback);

            // Trim输出
            auto trim = [](std::string& s) {
                s.erase(0, s.find_first_not_of(" \t\n\r"));
                s.erase(s.find_last_not_of(" \t\n\r") + 1);
            };
            trim(full_reply);

            // 4. 添加完整回复到历史
            sm.addAssistantMessage(session_id, full_reply);

            // 5. 发送最终的 done 事件
            sse_stream << "data: {\"done\":true,\"message\":\"" << escapeJson(full_reply) << "\"}\n\n";

            // 6. 返回 SSE 响应
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "text/event-stream; charset=utf-8");
            resp.setHeader("Cache-Control", "no-cache");
            resp.setHeader("Connection", "keep-alive");
            resp.setBody(sse_stream.str());
            return resp;
        } catch (const std::exception& e) {
            std::cerr << "[/api/sessions/{id}/chat/stream] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, std::string("stream chat failed: ") + e.what());
        }
    };

    sessionDeleteHandler = [&sm](const HttpRequest& r) {
        try {
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            std::string session_id = path.substr(start);
            if (!sm.hasSession(session_id)) return HttpResponse::makeErrorResponse(404, "session not found");
            sm.deleteSession(session_id);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"status\":\"deleted\",\"session_id\":\"" + session_id + "\"}");
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("delete session failed: ") + e.what());
        }
    };

    sessionTitleHandler = [&sm](const HttpRequest& r) {
        try {
            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/title");
            std::string session_id = path.substr(start, end - start);
            if (!sm.hasSession(session_id)) return HttpResponse::makeErrorResponse(404, "session not found");

            auto js = r.parseJson();
            std::string new_title = js["title"];
            if (new_title.empty()) return HttpResponse::makeErrorResponse(400, "title is required");

            sm.updateSessionTitle(session_id, new_title);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"status\":\"updated\",\"session_id\":\"" + session_id + "\",\"title\":\"" + escapeJson(new_title) + "\"}");
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("update title failed: ") + e.what());
        }
    };
}

inline void Router::addStatic(const std::string& url, const std::string& file) {
    addRoute("GET", url, [file](const HttpRequest&) {
        return HttpResponse::makeFileResponse(file);
    });
}

inline void Router::setupStaticPages() {
    const char* ui_path = "UI/";
    addStatic("/login.html",    std::string(ui_path) + "login.html");
    addStatic("/register.html", std::string(ui_path) + "register.html");
    addStatic("/chat.html",     std::string(ui_path) + "chat.html");
    addStatic("/multichat.html", std::string(ui_path) + "multichat.html");
    addStatic("/style.css",     std::string(ui_path) + "style.css");
    addStatic("/multichat.css", std::string(ui_path) + "multichat.css");
    addStatic("/simple-api.js", std::string(ui_path) + "simple-api.js");

    addRoute("GET", "/", [](const HttpRequest&) {
        HttpResponse r(302);
        r.setHeader("Location", "/multichat.html");
        return r;
    });
}
