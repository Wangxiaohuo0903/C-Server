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
            case 200: return "OK"; // 请求成功，一切正常。
            case 404: return "Not Found"; // 找不到所请求的资源。
            // ... 其他状态码 ...
            case 201: return "Created"; // 请求成功并且创建了新资源。
            case 204: return "No Content"; // 请求已成功处理，但没有内容返回。
            case 301: return "Moved Permanently"; // 资源已被永久移动到新的URL。
            case 302: return "Found"; // 资源临时重定向。
            case 304: return "Not Modified"; // 资源未被修改，使用缓存即可。
            
            case 400: return "Bad Request"; // 客户端请求存在语法错误或无法完成请求。
            case 401: return "Unauthorized"; // 未授权，需要有效的身份验证凭证。
            case 403: return "Forbidden"; // 禁止访问，即使有身份验证也可能拒绝访问。
            case 405: return "Method Not Allowed"; // 不允许使用请求的方法（如GET、POST）访问资源。

            case 500: return "Internal Server Error"; // 服务器遇到了一个未曾预期的情况，导致无法完成请求。
            case 503: return "Service Unavailable"; // 服务器暂时无法处理请求，通常由于过载或维护。
            default: return "Unknown";
        }
    }

    int statusCode; // 响应状态码
    std::unordered_map<std::string, std::string> headers; // 响应头信息
    std::string body; // 响应体
};
