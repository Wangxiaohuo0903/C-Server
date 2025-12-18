#include "http/HttpResponse.h"
#include <sstream>

HttpResponse::HttpResponse(int code) : statusCode(code) {}

void HttpResponse::setStatusCode(int code) {
    statusCode = code;
}

void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    headers[name] = value;
}

void HttpResponse::setBody(const std::string& b) {
    body = b;
}

std::string HttpResponse::toString() const {
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

HttpResponse HttpResponse::makeErrorResponse(int code, const std::string& message) {
    HttpResponse response(code);
    response.setBody(message);
    return response;
}

HttpResponse HttpResponse::makeOkResponse(const std::string& message) {
    HttpResponse response(200);
    response.setBody(message);
    return response;
}

std::string HttpResponse::getStatusMessage() const {
    switch (statusCode) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default: return "Unknown";
    }
}
