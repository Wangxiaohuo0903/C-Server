#pragma once

#include <string>
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "JWTAuth.h"

/**
 * @brief 认证中间件 - 从 HTTP 请求中提取和验证 JWT token
 *
 * 用于保护需要认证的 API 端点
 * 从 "Authorization: Bearer <token>" header 中提取并验证 JWT
 */
class AuthMiddleware {
public:
    /**
     * @brief 从 HTTP 请求中提取 JWT token
     * @param req HTTP 请求对象
     * @return JWT token 字符串（如果不存在或格式错误返回空字符串）
     *
     * 期望的 header 格式: "Authorization: Bearer eyJhbGci..."
     */
    static std::string extractToken(const HttpRequest& req) {
        // 获取 Authorization header
        std::string auth_header = req.getHeader("Authorization");

        if (auth_header.empty()) {
            return "";
        }

        // 检查是否以 "Bearer " 开头（注意有空格）
        const std::string bearer_prefix = "Bearer ";
        if (auth_header.substr(0, bearer_prefix.length()) != bearer_prefix) {
            return "";
        }

        // 提取 token（去掉 "Bearer " 前缀）
        std::string token = auth_header.substr(bearer_prefix.length());

        // 去除可能的前后空格
        size_t start = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");

        if (start == std::string::npos) {
            return "";
        }

        return token.substr(start, end - start + 1);
    }

    /**
     * @brief 验证 HTTP 请求的 JWT 并返回用户名
     * @param req HTTP 请求对象
     * @return 用户名（验证失败返回空字符串）
     *
     * 此方法组合了 extractToken() 和 JWTAuth::validateToken()
     * 是最常用的认证方法
     */
    static std::string authenticate(const HttpRequest& req) {
        // 1. 提取 token
        std::string token = extractToken(req);

        if (token.empty()) {
            return "";
        }

        // 2. 验证 token 并获取用户名
        std::string username = JWTAuth::validateToken(token);

        return username;
    }

    /**
     * @brief 创建 401 Unauthorized 响应
     * @param message 错误消息（默认 "Unauthorized"）
     * @return HTTP 401 响应对象
     *
     * 用于在认证失败时返回统一的错误响应
     */
    static HttpResponse makeUnauthorizedResponse(const std::string& message = "Unauthorized") {
        HttpResponse resp(401);
        resp.setHeader("Content-Type", "text/plain; charset=utf-8");
        resp.setHeader("WWW-Authenticate", "Bearer realm=\"API\"");
        resp.setBody(message);
        return resp;
    }

    /**
     * @brief 创建 403 Forbidden 响应
     * @param message 错误消息（默认 "Forbidden"）
     * @return HTTP 403 响应对象
     *
     * 用于在权限不足时返回响应（例如用户试图访问别人的 session）
     */
    static HttpResponse makeForbiddenResponse(const std::string& message = "Forbidden") {
        HttpResponse resp(403);
        resp.setHeader("Content-Type", "text/plain; charset=utf-8");
        resp.setBody(message);
        return resp;
    }
};
