#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>

class HttpResponse {
public:
    HttpResponse(int code = 200) 
        : statusCode(code) 
    {
        // 默认 HTTP/1.1 和短连接
        version = "HTTP/1.1";
        headers["Connection"] = "close";
    }

    void setStatusCode(int code) {
        statusCode = code;
    }

    void setHeader(const std::string& name, const std::string& value) {
        headers[name] = value;
    }

    void setBody(const std::string& b) {
        body = b;
    }

    // 构造完整的 HTTP 响应字符串
    std::string toString() const {
        std::ostringstream oss;
        // 状态行
        oss << version << " " << statusCode << " " << getStatusMessage() << "\r\n";

        // 自动填充 Content-Length
        oss << "Content-Length: " << body.size() << "\r\n";

        // 如果用户设置了 Content-Type，保留；否则默认 text/plain
        if (headers.count("Content-Type") == 0) {
            oss << "Content-Type: text/plain\r\n";
        }

        // 输出用户自定义的其他头
        for (const auto& kv : headers) {
            // 跳过 Connection（已经在构造函数中默认设置）和 Content-Type（已输出）
            if (kv.first == "Connection" || kv.first == "Content-Type") continue;
            oss << kv.first << ": " << kv.second << "\r\n";
        }

        // 追加 Connection 和 Content-Type
        oss << "Connection: " << headers.at("Connection") << "\r\n";
        oss << "Content-Type: " 
            << (headers.count("Content-Type") ? headers.at("Content-Type") : "text/plain") 
            << "\r\n";

        // 头部结束
        oss << "\r\n";

        // 响应体
        oss << body;
        return oss.str();
    }

    // 构造一个错误响应（简易）
    static HttpResponse makeErrorResponse(int code, const std::string& message) {
        HttpResponse resp(code);
        resp.setBody(message);
        resp.setHeader("Content-Type", "text/plain");
        return resp;
    }

    // 构造一个 200 OK 响应
    static HttpResponse makeOkResponse(const std::string& message) {
        HttpResponse resp(200);
        resp.setBody(message);
        resp.setHeader("Content-Type", "text/plain");
        return resp;
    }

private:
    int statusCode;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    std::string getStatusMessage() const {
        switch (statusCode) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 500: return "Internal Server Error";
            default:  return "Unknown";
        }
    }
};
