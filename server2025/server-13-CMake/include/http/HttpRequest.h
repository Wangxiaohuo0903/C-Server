#pragma once

#include <string>
#include <unordered_map>

/**
 * @brief HTTP请求解析类
 *
 * 功能：
 * - 解析HTTP请求行、请求头、请求体
 * - 支持表单数据解析 (application/x-www-form-urlencoded)
 * - 支持JSON数据解析
 * - 支持URL查询参数解析
 */
class HttpRequest {
public:
    enum Method {
        GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };

    enum ParseState {
        REQUEST_LINE, HEADERS, BODY, FINISH
    };

    HttpRequest();

    bool parse(std::string request);
    std::unordered_map<std::string, std::string> parseFormBody() const;
    std::unordered_map<std::string, std::string> parseJson() const;
    std::unordered_map<std::string, std::string> getQuery() const;

    std::string getMethodString() const;
    const std::string& getPath() const;
    Method getMethod() const { return method; }
    const std::string& getBody() const { return body; }

private:
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);

    Method method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    ParseState state;
    std::string body;
};
