// http_request.h
#pragma once

#include <string>
#include <unordered_map>
#include <sstream>

// 定义 HttpRequest 类用于解析和存储HTTP请求
class HttpRequest {
public:
    // 枚举类型，定义HTTP请求的方法
    enum Method {
        GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };

    // 枚举类型，定义解析HTTP请求的不同阶段
    enum ParseState {
        REQUEST_LINE, HEADERS, BODY, FINISH
    };

    // 构造函数，初始化成员变量
    HttpRequest() : method(UNKNOWN), state(REQUEST_LINE) {}


    /*
    POST /login HTTP/1.1
    Host: localhost:7007
    Content-Type: application/x-www-form-urlencoded
    Content-Length: 30
    
    username=yuanshen&password=test1
    */
    // 解析整个HTTP请求的函数
    bool parse(std::string request) {
        std::istringstream iss(request);
        std::string line;
        bool result = true;

        // 按行读取请求，并根据当前解析状态处理每行
        while (std::getline(iss, line) && line != "\r") {
            if (state == REQUEST_LINE) {
                result = parseRequestLine(line); // 解析请求行
            } else if (state == HEADERS) {
                result = parseHeader(line); // 解析请求头
            }
            if (!result) {
                break; // 如果解析失败，则跳出循环
            }
        }

        // 如果请求方法是POST，则提取请求体
        if (method == POST) {
            body = request.substr(request.find("\r\n\r\n") + 4);
        }

        return result; // 返回解析结果
    }

    // 解析表单形式的请求体，返回键值对字典
    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST) return params;

        std::istringstream stream(body);
        std::string pair;

        // 按 & 分隔表单数据，解析为键值对
        while (std::getline(stream, pair, '&')) {
            std::size_t pos = pair.find('=');
            if (pos == std::string::npos) continue;
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;
        }

        return params; // 返回解析后的表单数据
    }

    // 获取HTTP请求方法的字符串表示
    std::string getMethodString() const {
        switch (method) {
            case GET: return "GET";
            case POST: return "POST";
            // ... 其他方法的字符串表示 ...
            default: return "UNKNOWN";
        }
    }

    // 获取请求路径的函数
    const std::string& getPath() const {
        return path;
    }

    // 其他成员函数和变量 ...

private:
    // 解析请求行的函数
    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line);
        std::string method_str;
        iss >> method_str;
        if (method_str == "GET") method = GET;
        else if (method_str == "POST") method = POST;
        else method = UNKNOWN;

        iss >> path; // 解析请求路径
        iss >> version; // 解析HTTP协议版本
        state = HEADERS; // 更新解析状态为解析请求头
        return true;
    }

    // 解析请求头的函数
    bool parseHeader(const std::string& line) {
        size_t pos = line.find(": ");
        if (pos == std::string::npos) {
            return false; // 如果格式不正确，则解析失败
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 2);
        headers[key] = value; // 存储键值对到headers字典
        return true;
    }

    Method method; // 请求方法
    std::string path; // 请求路径
    std::string version; // HTTP协议版本
    std::unordered_map<std::string, std::string> headers; // 请求头
    ParseState state; // 请求解析状态
    std::string body; // 请求体
};
