#pragma once

#include <string>
#include <unordered_map>

/**
 * @brief HTTP响应类
 *
 * 功能：
 * - 设置状态码、响应头、响应体
 * - 自动添加Content-Length和Connection头
 * - 生成符合HTTP/1.1标准的响应字符串
 */
class HttpResponse {
public:
    HttpResponse(int code = 200);

    void setStatusCode(int code);
    void setHeader(const std::string& name, const std::string& value);
    void setBody(const std::string& b);

    std::string toString() const;

    static HttpResponse makeErrorResponse(int code, const std::string& message);
    static HttpResponse makeOkResponse(const std::string& message);

private:
    std::string getStatusMessage() const;

    int statusCode;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};
