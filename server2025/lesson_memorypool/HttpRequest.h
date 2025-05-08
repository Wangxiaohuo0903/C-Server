// http_request.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

// HttpRequest 类用于表示一个 HTTP 请求，包含解析请求行、请求头、请求体等操作
class HttpRequest {
public:
    // 定义 HTTP 方法类型的枚举，包括 GET、POST 等常见方法
    enum Method {
        GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };

    // 定义 HTTP 请求解析的状态机状态
    enum ParseState {
        REQUEST_LINE, HEADERS, BODY, FINISH
    };

    // 构造函数：初始化 HTTP 请求对象，默认方法为 UNKNOWN，状态为 REQUEST_LINE
    HttpRequest() : method(UNKNOWN), state(REQUEST_LINE) {}

    // 解析 HTTP 请求字符串
    // 输入：完整的 HTTP 请求字符串
    // 返回：解析是否成功
    bool parse(std::string request) {
        std::istringstream iss(request); // 将请求字符串转换为输入流
        std::string line;  // 存储逐行读取的请求数据
        bool result = true;

        // 逐行读取请求数据，直到遇到空行（请求头结束）
        while (std::getline(iss, line) && line != "\r") {
            // 根据当前状态解析请求行或请求头
            if (state == REQUEST_LINE) {
                result = parseRequestLine(line); // 解析请求行
            } else if (state == HEADERS) {
                result = parseHeader(line); // 解析请求头
            }
            // 如果解析失败，则终止解析
            if (!result) {
                break;
            }
        }

        // 如果是 POST 方法，还需要解析请求体
        if (method == POST) {
            // 提取请求体部分
            body = request.substr(request.find("\r\n\r\n") + 4);
        }

        return result; // 返回解析结果
    }

    // 解析表单数据（仅适用于 POST 请求）
    // 返回：一个包含表单键值对的 unordered_map
    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST) return params; // 只有 POST 请求才会有表单数据

        // 使用 & 分割表单项，进一步解析每个键值对
        std::istringstream stream(body);
        std::string pair;

        while (std::getline(stream, pair, '&')) {
            std::size_t pos = pair.find('=');
            if (pos == std::string::npos) continue; // 如果没有找到 '='，跳过该项
            std::string key = pair.substr(0, pos);        // 键
            std::string value = pair.substr(pos + 1);     // 值
            params[key] = value;                          // 存储键值对
        }

        return params; // 返回解析后的键值对
    }

    // 获取 HTTP 方法的字符串表示（如 GET、POST 等）
    std::string getMethodString() const {
        switch (method) {
            case GET: return "GET";
            case POST: return "POST";
            // ... 其他方法可以继续添加
            default: return "UNKNOWN"; // 未知的方法返回 "UNKNOWN"
        }
    }

    // 获取请求的路径（即 URL 中的路径部分）
    const std::string& getPath() const {
        return path;
    }

    // 重置 HTTP 请求对象的状态，为下一次请求准备
    void reset() {
        method = UNKNOWN;         // 将方法重置为未知
        path.clear();              // 清空路径
        version.clear();           // 清空协议版本
        headers.clear();           // 清空请求头
        state = REQUEST_LINE;      // 设置状态为请求行解析
        body.clear();              // 清空请求体
    }

private:
    // 解析请求行（例如：GET /path HTTP/1.1）
    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line);
        std::string method_str;
        iss >> method_str; // 读取请求方法（如 GET、POST 等）
        // 根据方法字符串设置请求方法
        if (method_str == "GET") method = GET;
        else if (method_str == "POST") method = POST;
        else method = UNKNOWN; // 如果方法未知，设置为 UNKNOWN

        iss >> path;      // 读取请求路径
        iss >> version;   // 读取协议版本（如 HTTP/1.1）
        state = HEADERS;  // 设置状态为请求头解析
        return true;
    }

    // 解析请求头（格式：Key: Value）
    bool parseHeader(const std::string& line) {
        size_t pos = line.find(": "); // 查找键和值之间的分隔符 ": "
        if (pos == std::string::npos) {
            return false; // 如果找不到 ": "，说明这不是有效的头部
        }
        std::string key = line.substr(0, pos);        // 头部字段的键
        std::string value = line.substr(pos + 2);     // 头部字段的值
        headers[key] = value;                          // 将头部键值对存入 headers
        return true;
    }

    // HTTP 请求的相关数据
    Method method;                                       // 请求方法（如 GET、POST）
    std::string path;                                    // 请求的路径
    std::string version;                                 // 请求的协议版本（如 HTTP/1.1）
    std::unordered_map<std::string, std::string> headers; // 存储请求头字段
    ParseState state;                                    // 当前解析状态
    std::string body;                                    // 请求体（如 POST 请求中的数据）
};
