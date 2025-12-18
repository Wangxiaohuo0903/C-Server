// http_response.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

class HttpResponse {
public:
    HttpResponse(int code = 200) : statusCode(code) {}

    void setStatusCode(int code) {
        statusCode = code;
    }

    void setHeader(const std::string& name, const std::string& value) {
        headers[name] = value;
    }

    void setBody(const std::string& b) {
        body = b;
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << statusCode << " " << getStatusMessage() << "\r\n";

        // 自动添加Content-Length头（修复连接不关闭的问题）
        oss << "Content-Length: " << body.size() << "\r\n";

        // 添加Connection: close头（明确告知客户端连接会关闭）
        oss << "Connection: close\r\n";

        // 添加用户自定义的headers
        for (const auto& header : headers) {
            // 避免重复添加Content-Length和Connection
            if (header.first != "Content-Length" && header.first != "Connection") {
                oss << header.first << ": " << header.second << "\r\n";
            }
        }

        oss << "\r\n" << body;
        return oss.str();
    }

    static HttpResponse makeErrorResponse(int code, const std::string& message) {
        HttpResponse response(code);
        response.setBody(message);
        return response;
    }
    static HttpResponse makeOkResponse(const std::string& message) {
        HttpResponse response(200);
        response.setBody(message);
        return response;
    }

private:
    std::string getStatusMessage() const {
        switch (statusCode) {
            case 200: return "OK";
            case 404: return "Not Found";
            // ... 其他状态码 ...
            default: return "Unknown";
        }
    }

    int statusCode;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};
