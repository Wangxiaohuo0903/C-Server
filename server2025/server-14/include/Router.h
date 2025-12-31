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
#include "JWTAuth.h"          // Server-14: JWT authentication
#include "AuthMiddleware.h"    // Server-14: Authentication middleware

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

// Server-14: Clean model output by removing template tags (including DeepSeek-R1 tags)
inline std::string cleanModelOutput(std::string text) {
    // Remove complete template tags first (including DeepSeek-R1 tags and closing variants)
    const std::vector<std::string> complete_tags = {
        "<|im_start|>", "<|im_end|>", "<|endoftext|>",
        "<|end|>", "<|begin|>", "<|start|>",
        "<|user|>", "<|assistant|>", "<|system|>",
        "<|User|>", "<|Assistant|>", "<|System|>",  // DeepSeek-R1 tags
        "</|User|>", "</|Assistant|>", "</|System|>",  // DeepSeek-R1 closing tags
        "</|User|>>", "</|Assistant|>>", "</|System|>>",  // With extra >
        "<think>", "</think>", "<reasoning>", "</reasoning>",  // DeepSeek-R1 thinking tags
        "<s>", "</s>", "[INST]", "[/INST]",
        "im_start", "im_end"
    };

    for (const auto& tag : complete_tags) {
        size_t pos = 0;
        while ((pos = text.find(tag, pos)) != std::string::npos) {
            text.erase(pos, tag.length());
        }
    }

    // Remove standalone template markers (with word boundaries)
    const std::vector<std::string> standalone_markers = {
        "\nuser\n", "\nassistant\n", "\nsystem\n",
        "\nUser\n", "\nAssistant\n", "\nSystem\n",
        "\nuser", "\nassistant", "\nsystem",
        "\nUser", "\nAssistant", "\nSystem",
        "user\n", "assistant\n", "system\n",
        "User\n", "Assistant\n", "System\n",
        "\nthink\n", "\nreasoning\n"
    };

    for (const auto& marker : standalone_markers) {
        size_t pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            text.erase(pos, marker.length());
            if (marker.find('\n') != std::string::npos) {
                text.insert(pos, "\n");
            }
        }
    }

    // Remove any remaining partial tags (but be careful not to remove parts of words)
    // Only remove if they appear at the start or end of the text
    const std::vector<std::string> edge_tags = {"<|", "|>", "|>>"};
    for (const auto& tag : edge_tags) {
        // Remove from start
        while (text.find(tag) == 0) {
            text.erase(0, tag.length());
        }
        // Remove from end
        while (text.length() >= tag.length() &&
               text.substr(text.length() - tag.length()) == tag) {
            text.erase(text.length() - tag.length());
        }
    }

    // Trim leading/trailing whitespace
    text.erase(0, text.find_first_not_of(" \t\n\r"));
    text.erase(text.find_last_not_of(" \t\n\r") + 1);

    // If result is empty or just whitespace, return a friendly message
    if (text.empty() || text.find_first_not_of(" \t\n\r") == std::string::npos) {
        return "你好！我是 AI 助手，有什么可以帮到你的吗？";
    }

    return text;
}

// Server-14: Structure to hold separated thinking process and final answer
struct ThinkingAndAnswer {
    std::string thinking;  // Content inside <think>...</think>
    std::string answer;    // Content outside thinking tags
    bool has_thinking;     // Whether thinking process was found
};

// Server-14: Separate thinking process from final answer (for DeepSeek-R1)
inline ThinkingAndAnswer separateThinkingAndAnswer(const std::string& text) {
    ThinkingAndAnswer result;
    result.has_thinking = false;

    // Find <think> and </think> tags
    size_t think_start = text.find("<think>");
    size_t think_end = text.find("</think>");

    if (think_start != std::string::npos && think_end != std::string::npos && think_end > think_start) {
        // Extract thinking process (content between tags)
        result.thinking = text.substr(think_start + 7, think_end - think_start - 7);

        // Extract answer (everything before <think> and after </think>)
        std::string before = text.substr(0, think_start);
        std::string after = text.substr(think_end + 8);
        result.answer = before + after;

        result.has_thinking = true;
    } else {
        // No thinking tags found, entire text is the answer
        result.thinking = "";
        result.answer = text;
        result.has_thinking = false;
    }

    // Clean both parts
    result.thinking.erase(0, result.thinking.find_first_not_of(" \t\n\r"));
    result.thinking.erase(result.thinking.find_last_not_of(" \t\n\r") + 1);

    result.answer.erase(0, result.answer.find_first_not_of(" \t\n\r"));
    result.answer.erase(result.answer.find_last_not_of(" \t\n\r") + 1);

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
    // Server-14: Modified to return JWT token
    addRoute("POST", "/login", [&db](const HttpRequest& r) {
        auto p = r.parseFormBody();
        if (p["username"].empty() || p["password"].empty())
            return HttpResponse::makeErrorResponse(400, "missing field");

        bool ok = db.loginUser(p["username"], p["password"]);
        if (!ok) {
            return HttpResponse::makeErrorResponse(403, "bad credential");
        }

        // Generate JWT token
        std::string token = JWTAuth::generateToken(p["username"]);

        // Return JSON with token and username
        HttpResponse resp(200);
        resp.setHeader("Content-Type", "application/json; charset=utf-8");
        resp.setBody("{\"token\":\"" + token + "\",\"username\":\"" + escapeJson(p["username"]) + "\"}");
        return resp;
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

// Server-14: Session routes with JWT authentication
inline void Router::setupSessionRoutes(SessionManager& sm, ModelManagerV2& mm) {
    // Server-14: Create new session (requires authentication)
    addRoute("POST", "/api/sessions/new", [&sm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            // Create session for authenticated user
            std::string session_id = sm.createSession(username);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody("{\"session_id\":\"" + session_id + "\",\"status\":\"created\"}");
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("create session failed: ") + e.what());
        }
    });

    // Server-14: Get all sessions for authenticated user only
    addRoute("GET", "/api/sessions", [&sm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            // Get only this user's sessions
            std::string sessions_json = sm.getAllSessionsForUser(username);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody(sessions_json);
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get sessions failed: ") + e.what());
        }
    });

    // Server-14: Get session history (requires authentication and ownership)
    sessionHistoryHandler = [&sm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/history");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // Verify session ownership
            if (!sm.verifySessionOwnership(session_id, username)) {
                return AuthMiddleware::makeForbiddenResponse("Access denied");
            }

            std::string history_json = sm.getSessionHistory(session_id);
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            resp.setBody(history_json);
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("get history failed: ") + e.what());
        }
    };

    // Server-14: Chat endpoint (requires authentication and ownership)
    sessionChatHandler = [&sm, &mm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/chat");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // Verify session ownership
            if (!sm.verifySessionOwnership(session_id, username)) {
                return AuthMiddleware::makeForbiddenResponse("Access denied");
            }

            auto js = r.parseJson();
            std::string user_message = js["message"];
            if (user_message.empty()) {
                return HttpResponse::makeErrorResponse(400, "message is required");
            }

            // DeepSeek-R1 需要更多 tokens 用于"思考"阶段，默认 8192
            int maxTokens = js["max_tokens"].empty() ? 8192 : std::stoi(js["max_tokens"]);
            // DeepSeek-R1 推荐温度 0.6，不要太高
            float temperature = js["temperature"].empty() ? 0.6f : std::stof(js["temperature"]);

            // 1. Add user message to UI session manager (will be persisted to DB)
            sm.addUserMessage(session_id, user_message);

            // 2. Get full conversation history to build the prompt
            std::string full_prompt = sm.getRecentContext(session_id, 20);

            // 3. Call ModelManagerV2 with cache enabled
            std::string assistant_reply = mm.inferWithCache(session_id, full_prompt, maxTokens, temperature);

            // Server-14: 调试 - 打印模型原始输出
            std::cerr << "\n========== MODEL RAW OUTPUT ==========\n";
            std::cerr << assistant_reply << "\n";
            std::cerr << "======================================\n\n";

            // 4. Separate thinking process from final answer (Server-14: DeepSeek-R1 feature)
            ThinkingAndAnswer parsed = separateThinkingAndAnswer(assistant_reply);

            std::cerr << "[Router] After separation:\n";
            std::cerr << "  has_thinking: " << parsed.has_thinking << "\n";
            std::cerr << "  thinking (raw): " << parsed.thinking << "\n";
            std::cerr << "  answer (raw): " << parsed.answer << "\n";

            // Clean both thinking and answer parts
            parsed.thinking = cleanModelOutput(parsed.thinking);
            parsed.answer = cleanModelOutput(parsed.answer);

            std::cerr << "[Router] After cleaning:\n";
            std::cerr << "  thinking (cleaned): " << parsed.thinking << "\n";
            std::cerr << "  answer (cleaned): " << parsed.answer << "\n";

            // 5. Add the final answer to session manager (will be persisted to DB)
            // Note: We only persist the answer, not the thinking process
            sm.addAssistantMessage(session_id, parsed.answer);

            // 6. Return JSON response with separated thinking and answer
            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json; charset=utf-8");
            std::ostringstream json_resp;
            json_resp << "{"
                      << "\"session_id\":\"" << session_id << "\",\n"
                      << "\"message\":\"" << escapeJson(parsed.answer) << "\",\n"
                      << "\"thinking\":\"" << escapeJson(parsed.thinking) << "\",\n"
                      << "\"has_thinking\":" << (parsed.has_thinking ? "true" : "false") << ",\n"
                      << "\"message_count\":" << sm.getMessageCount(session_id)
                      << "}";
            resp.setBody(json_resp.str());
            return resp;
        } catch (const std::exception& e) {
            std::cerr << "[/api/sessions/{id}/chat] EXCEPTION: " << e.what() << "\n";
            return HttpResponse::makeErrorResponse(500, std::string("chat failed: ") + e.what());
        }
    };

    // Server-14: Streaming chat handler with authentication
    sessionChatStreamHandler = [&sm, &mm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/chat/stream");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // Verify session ownership
            if (!sm.verifySessionOwnership(session_id, username)) {
                return AuthMiddleware::makeForbiddenResponse("Access denied");
            }

            auto js = r.parseJson();
            std::string user_message = js["message"];
            if (user_message.empty()) {
                return HttpResponse::makeErrorResponse(400, "message is required");
            }

            // DeepSeek-R1 需要更多 tokens 用于"思考"阶段，默认 8192
            int maxTokens = js["max_tokens"].empty() ? 8192 : std::stoi(js["max_tokens"]);
            // DeepSeek-R1 推荐温度 0.6，不要太高
            float temperature = js["temperature"].empty() ? 0.6f : std::stof(js["temperature"]);

            // 1. Add user message (will be persisted to DB)
            sm.addUserMessage(session_id, user_message);

            // 2. Get conversation context
            std::string full_prompt = sm.getRecentContext(session_id, 20);

            // 3. Stream inference - collect all SSE events with thinking detection
            std::ostringstream sse_stream;
            std::string full_reply;
            bool in_thinking_mode = false;
            std::string current_token_buffer;

            auto callback = [&](const std::string& token) -> bool {
                full_reply += token;
                current_token_buffer += token;

                // Check for thinking tags
                if (current_token_buffer.find("<think>") != std::string::npos) {
                    in_thinking_mode = true;
                    // Clear the buffer after detecting the tag
                    size_t pos = current_token_buffer.find("<think>");
                    current_token_buffer = current_token_buffer.substr(pos + 7);
                }

                if (current_token_buffer.find("</think>") != std::string::npos) {
                    in_thinking_mode = false;
                    // Clear the buffer after detecting the tag
                    size_t pos = current_token_buffer.find("</think>");
                    current_token_buffer = current_token_buffer.substr(pos + 8);
                }

                // Send token with type indicator (for frontend to distinguish)
                std::string token_type = in_thinking_mode ? "thinking" : "answer";
                sse_stream << "data: {\"token\":\"" << escapeJson(token)
                          << "\",\"type\":\"" << token_type << "\"}\n\n";

                // Keep buffer size manageable
                if (current_token_buffer.length() > 100) {
                    current_token_buffer = current_token_buffer.substr(current_token_buffer.length() - 50);
                }

                return true; // Continue generation
            };

            // Call streaming inference
            mm.inferWithCacheStreaming(session_id, full_prompt, maxTokens, temperature, callback);

            // 4. Separate thinking from answer
            ThinkingAndAnswer parsed = separateThinkingAndAnswer(full_reply);
            parsed.thinking = cleanModelOutput(parsed.thinking);
            parsed.answer = cleanModelOutput(parsed.answer);

            // 5. Add final answer to history (will be persisted to DB)
            sm.addAssistantMessage(session_id, parsed.answer);

            // 6. Send final done event with separated thinking and answer
            sse_stream << "data: {\"done\":true"
                      << ",\"message\":\"" << escapeJson(parsed.answer) << "\""
                      << ",\"thinking\":\"" << escapeJson(parsed.thinking) << "\""
                      << ",\"has_thinking\":" << (parsed.has_thinking ? "true" : "false")
                      << "}\n\n";

            // 7. Return SSE response
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

    // Server-14: Delete session (requires authentication and ownership)
    sessionDeleteHandler = [&sm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            std::string session_id = path.substr(start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // Verify session ownership
            if (!sm.verifySessionOwnership(session_id, username)) {
                return AuthMiddleware::makeForbiddenResponse("Access denied");
            }

            sm.deleteSession(session_id);

            HttpResponse resp(200);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"status\":\"deleted\",\"session_id\":\"" + session_id + "\"}");
            return resp;
        } catch (const std::exception& e) {
            return HttpResponse::makeErrorResponse(500, std::string("delete session failed: ") + e.what());
        }
    };

    // Server-14: Update session title (requires authentication and ownership)
    sessionTitleHandler = [&sm](const HttpRequest& r) {
        try {
            // Authenticate user
            std::string username = AuthMiddleware::authenticate(r);
            if (username.empty()) {
                return AuthMiddleware::makeUnauthorizedResponse();
            }

            std::string path = r.getPath();
            size_t start = path.find("/api/sessions/") + 14;
            size_t end = path.find("/title");
            std::string session_id = path.substr(start, end - start);

            if (!sm.hasSession(session_id)) {
                return HttpResponse::makeErrorResponse(404, "session not found");
            }

            // Verify session ownership
            if (!sm.verifySessionOwnership(session_id, username)) {
                return AuthMiddleware::makeForbiddenResponse("Access denied");
            }

            auto js = r.parseJson();
            std::string new_title = js["title"];
            if (new_title.empty()) {
                return HttpResponse::makeErrorResponse(400, "title is required");
            }

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
    addStatic("/login.css",     std::string(ui_path) + "login.css");  // Server-14: login page CSS
    addStatic("/register.html", std::string(ui_path) + "register.html");
    addStatic("/chat.html",     std::string(ui_path) + "chat.html");
    addStatic("/multichat.html", std::string(ui_path) + "multichat.html");
    addStatic("/style.css",     std::string(ui_path) + "style.css");
    addStatic("/multichat.css", std::string(ui_path) + "multichat.css");
    addStatic("/simple-api.js", std::string(ui_path) + "simple-api.js");

    // Server-14: Redirect to login page if not authenticated
    addRoute("GET", "/", [](const HttpRequest&) {
        HttpResponse r(302);
        r.setHeader("Location", "/login.html");
        return r;
    });
}
