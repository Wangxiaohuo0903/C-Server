// http_response.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

class HttpResponse {
public:
    // 构造函数，默认状态码为 200
    HttpResponse(int code = 200) : statusCode(code) {}

    // 设置状态码
    void setStatusCode(int code) {
        statusCode = code;
    }

    // 设置响应头
    void setHeader(const std::string& name, const std::string& value) {
        headers[name] = value;
    }

    // 设置响应体
    void setBody(const std::string& b) {
        body = b;
    }

    // 将响应转换为字符串
    std::string toString() const {
        std::ostringstream oss;
        // 添加 HTTP 头信息：协议版本、状态码、状态消息
        oss << "HTTP/1.1 " << statusCode << " " << getStatusMessage() << "\r\n";
        // 添加其他响应头
        for (const auto& header : headers) {
            oss << header.first << ": " << header.second << "\r\n";
        }
        // 添加空行分隔响应头和响应体
        oss << "\r\n" << body;
        return oss.str();
    }

    // 创建一个包含错误信息的响应
    static HttpResponse makeErrorResponse(int code, const std::string& message) {
        HttpResponse response(code);
        response.setBody(message);
        return response;
    }

    // 创建一个包含成功信息的响应
    static HttpResponse makeOkResponse(const std::string& message) {
        HttpResponse response(200);
        response.setBody(message);
        return response;
    }

private:
    // 获取状态消息（根据状态码）
    std::string getStatusMessage() const {
        switch (statusCode) {
            case 200: return "OK";
            case 404: return "Not Found";
            // ... 其他状态码 ...
            default: return "Unknown";
        }
    }

    int statusCode; // 响应状态码
    std::unordered_map<std::string, std::string> headers; // 响应头信息
    std::string body; // 响应体
};
