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
        REQUEST_LINE,  // 解析请求行阶段
        HEADERS,       // 解析请求头阶段
        BODY,          // 解析请求体阶段
        FINISH         // 解析完成
    };

    // 构造函数，初始化成员变量
    HttpRequest() : method(UNKNOWN), state(REQUEST_LINE) {}

    // 解析整个HTTP请求的函数
    bool parse(const std::string& request) {
        // 使用 istringstream 按行读取
        std::istringstream iss(request);
        std::string line;
        bool result = true;

        // 按行读取请求，并根据当前解析状态处理每行
        while (std::getline(iss, line) && line != "\r") {
            // 去掉行尾的 \r（如果 getline 没去掉）
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (state == REQUEST_LINE) {
                // 解析请求行
                result = parseRequestLine(line);
            } 
            else if (state == HEADERS) {
                // 解析请求头
                result = parseHeader(line);
            }

            if (!result) {
                // 如果解析失败，跳出循环
                break;
            }
        }

        // 如果请求方法是 POST，则将剩余部分当作请求体（简单处理）
        if (method == POST) {
            std::size_t pos = request.find("\r\n\r\n");
            if (pos != std::string::npos) {
                // 把 \r\n\r\n 之后的全部内容当作 body
                body = request.substr(pos + 4);
            }
        }

        return result; // 返回解析结果
    }

    // 解析表单形式 (application/x-www-form-urlencoded) 的请求体，返回键值对字典
    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST) {
            return params;
        }

        std::istringstream stream(body);
        std::string pair;

        // 按 & 分隔表单数据，解析为键值对
        while (std::getline(stream, pair, '&')) {
            std::size_t pos = pair.find('=');
            if (pos == std::string::npos) continue;
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            // 这里没有做 URL decode，如有需要请自行解码
            params[key] = value;
        }

        return params; // 返回解析后的表单数据
    }

    // 获取 HTTP 请求方法的字符串表示
    std::string getMethodString() const {
        switch (method) {
            case GET:    return "GET";
            case POST:   return "POST";
            case HEAD:   return "HEAD";
            case PUT:    return "PUT";
            case DELETE: return "DELETE";
            case TRACE:  return "TRACE";
            case OPTIONS:return "OPTIONS";
            case CONNECT:return "CONNECT";
            case PATCH:  return "PATCH";
            default:     return "UNKNOWN";
        }
    }

    // 获取（仅）请求路径，例如 /download
    const std::string& getPath() const {
        return path;
    }

    // 获取整个查询字符串部分，例如 filename=HttpServer.h
    // （不包含 '?'）
    const std::string& getQuery() const {
        return query;
    }

    // 返回完整 body（如果有）
    const std::string& getBody() const {
        return body;
    }

    // 获取指定的请求头
    std::string getHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second;
        }
        return "";
    }

    // 判断是否解析完成
    bool isFinished() const {
        return state == FINISH;
    }

    // 获取内部枚举表示的方法
    Method getMethod() const {
        return method;
    }

private:
    // 解析请求行，例如
    // "GET /download?filename=HttpServer.h HTTP/1.1"
    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line);
        std::string method_str;
        std::string rawUri; // 例：/download?filename=HttpServer.h

        iss >> method_str;
        if      (method_str == "GET")     method = GET;
        else if (method_str == "POST")    method = POST;
        else if (method_str == "HEAD")    method = HEAD;
        else if (method_str == "PUT")     method = PUT;
        else if (method_str == "DELETE")  method = DELETE;
        else if (method_str == "TRACE")   method = TRACE;
        else if (method_str == "OPTIONS") method = OPTIONS;
        else if (method_str == "CONNECT") method = CONNECT;
        else if (method_str == "PATCH")   method = PATCH;
        else                              method = UNKNOWN;

        iss >> rawUri;    // e.g.  /download?filename=HttpServer.h
        iss >> version;   // e.g.  HTTP/1.1

        // 在 rawUri 中寻找 '?'
        auto qPos = rawUri.find('?');
        if (qPos == std::string::npos) {
            // 没有 '?'
            path = rawUri;
            query.clear();
        } else {
            // 例如 /download?filename=HttpServer.h
            path = rawUri.substr(0, qPos);      // /download
            query = rawUri.substr(qPos + 1);    // filename=HttpServer.h
        }

        // 更新解析状态：转到解析请求头
        state = HEADERS;
        return true;
    }

    // 解析请求头的函数
    bool parseHeader(const std::string& line) {
        size_t pos = line.find(": ");
        if (pos == std::string::npos) {
            // 如果是空行就表示 headers 解析结束
            if (line.empty()) {
                state = BODY; // 下一个阶段是 BODY
                return true; 
            }
            // 格式不正确，则返回 false
            return false; 
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 2);
        headers[key] = value; // 存储键值对到 headers 字典
        return true;
    }

private:
    Method method;                     // 请求方法
    ParseState state;                  // 请求解析状态
    std::string path;                  // 请求路径（不含查询字符串）
    std::string query;                 // 查询字符串（不含 '?'）
    std::string version;               // HTTP 协议版本
    std::unordered_map<std::string, std::string> headers; // 请求头
    std::string body;                  // 请求体
};
