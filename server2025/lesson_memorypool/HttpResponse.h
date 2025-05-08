// http_response.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

// HttpResponse 类用于表示一个 HTTP 响应，负责构建并返回格式化的 HTTP 响应字符串。
class HttpResponse {
public:
    // 构造函数：初始化 HTTP 响应状态码，默认值为 200（表示成功）
    HttpResponse(int code = 200) : statusCode(code) {}

    // 设置 HTTP 响应的状态码
    void setStatusCode(int code) {
        statusCode = code;
    }

    // 设置 HTTP 响应的头部字段
    // name: 头部字段的名称（如 "Content-Type"）
    // value: 头部字段的值（如 "text/html"）
    void setHeader(const std::string& name, const std::string& value) {
        headers[name] = value;  // 将头部字段名称和值存入 unordered_map
    }

    // 设置 HTTP 响应的正文内容
    void setBody(const std::string& b) {
        body = b;  // 将响应体内容赋值给 body
    }

    // 将 HTTP 响应转换为字符串，格式符合 HTTP 协议标准
    std::string toString() const {
        std::ostringstream oss;  // 使用字符串流拼接响应字符串
        // 拼接响应的状态行，例如 "HTTP/1.1 200 OK"
        oss << "HTTP/1.1 " << statusCode << " " << getStatusMessage() << "\r\n";
        
        // 拼接所有响应头部字段（每个字段以 "\r\n" 结束）
        for (const auto& header : headers) {
            oss << header.first << ": " << header.second << "\r\n";
        }
        
        // 拼接空行，表示头部和正文的分隔
        oss << "\r\n";
        
        // 拼接响应正文
        oss << body;

        return oss.str();  // 返回拼接后的字符串，表示完整的 HTTP 响应
    }

    // 静态方法：创建一个错误的 HTTP 响应
    // code: 错误的 HTTP 状态码
    // message: 错误的详细信息
    static HttpResponse makeErrorResponse(int code, const std::string& message) {
        HttpResponse response(code);  // 创建一个指定状态码的响应
        response.setBody(message);    // 设置响应体为错误信息
        return response;              // 返回构建好的错误响应
    }

    // 静态方法：创建一个成功的 HTTP 响应，状态码为 200
    static HttpResponse makeOkResponse(const std::string& message) {
        HttpResponse response(200);  // 创建状态码为 200（OK）的响应
        response.setBody(message);   // 设置响应体为成功信息
        return response;             // 返回构建好的成功响应
    }

    // 重置 HTTP 响应对象，恢复到初始状态
    void reset() {
        statusCode = 200;          // 状态码恢复为 200（OK）
        headers.clear();           // 清空所有头部字段
        body.clear();              // 清空响应体
    }

private:
    // 根据状态码获取对应的状态消息（如 200 -> "OK"，404 -> "Not Found"）
    std::string getStatusMessage() const {
        switch (statusCode) {
            case 200: return "OK";              // 状态码 200 对应 "OK"
            case 404: return "Not Found";       // 状态码 404 对应 "Not Found"
            // 其他状态码可以继续扩展
            default: return "Unknown";          // 默认返回 "Unknown"
        }
    }

    int statusCode;                                      // HTTP 响应的状态码
    std::unordered_map<std::string, std::string> headers;  // 存储响应头部字段的键值对
    std::string body;                                     // 存储响应体的内容
};
