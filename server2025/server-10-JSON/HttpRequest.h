// http_request.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>


class HttpRequest {
public:
    enum Method {
        GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };
    enum ParseState {
        REQUEST_LINE, HEADERS, BODY, FINISH
    };

    HttpRequest() : method(UNKNOWN), state(REQUEST_LINE) {}

    bool parse(std::string request) {
        std::istringstream iss(request);
        std::string line;
        bool result = true;

        while (std::getline(iss, line) && line != "\r") {
            if (state == REQUEST_LINE) {
                result = parseRequestLine(line);
            } else if (state == HEADERS) {
                result = parseHeader(line);
            }
            if (!result) {
                break;
            }
        }

        if (method == POST) {
            body = request.substr(request.find("\r\n\r\n") + 4);
        }

        return result;
    }

    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST) return params;

        std::istringstream stream(body);
        std::string pair;

        while (std::getline(stream, pair, '&')) {
            std::size_t pos = pair.find('=');
            if (pos == std::string::npos) continue;
            std::string key = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;
        }

        return params;
    }

    // ★ 新增：解析JSON格式的body
    // 支持简单的JSON格式: {"key":"value", "key2":"value2"}
    // 注意：这是一个简化版的JSON解析器，仅支持字符串类型的键值对
    // 不支持嵌套对象、数组、数字、布尔值等复杂类型
    std::unordered_map<std::string, std::string> parseJson() const {
        std::unordered_map<std::string, std::string> result;
        std::string json = body;

        /* ========================================
         * 步骤1: 去除首尾的空白字符和大括号
         * 输入: {"username":"alice","password":"123"}
         * 输出: "username":"alice","password":"123"
         * ======================================== */
        size_t start = json.find('{');
        size_t end = json.rfind('}');
        if (start == std::string::npos || end == std::string::npos) {
            return result; // JSON格式错误，返回空map
        }
        json = json.substr(start + 1, end - start - 1);

        /* ========================================
         * 步骤2: 逐个解析键值对
         * 解析模式: "key":"value"
         * ======================================== */
        size_t pos = 0;
        while (pos < json.length()) {
            // 跳过空白字符（空格、换行、回车、制表符）
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
                pos++;
            }
            if (pos >= json.length()) break;

            /* --- 提取键（key）--- */
            // 查找键的开始引号 "key"
            if (json[pos] != '\"') {
                pos++;
                continue;
            }
            size_t key_start = pos + 1;  // 引号后的第一个字符
            size_t key_end = json.find('\"', key_start);  // 查找结束引号
            if (key_end == std::string::npos) break;  // 没有结束引号，格式错误
            std::string key = json.substr(key_start, key_end - key_start);  // 提取键名

            /* --- 查找冒号分隔符 --- */
            // 例如: "username":"alice"
            //                  ^ 这里是冒号
            pos = json.find(':', key_end);
            if (pos == std::string::npos) break;  // 没有冒号，格式错误
            pos++;  // 跳过冒号

            // 跳过冒号后的空白（例如: ": " 中的空格）
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
                pos++;
            }

            /* --- 提取值（value）--- */
            // 查找值的开始引号 "value"
            if (pos >= json.length() || json[pos] != '\"') {
                pos++;
                continue;
            }
            size_t val_start = pos + 1;  // 引号后的第一个字符
            size_t val_end = json.find('\"', val_start);  // 查找结束引号
            if (val_end == std::string::npos) break;  // 没有结束引号，格式错误
            std::string value = json.substr(val_start, val_end - val_start);  // 提取值

            /* --- 存储键值对 --- */
            result[key] = value;
            pos = val_end + 1;  // 移动到值的结束引号之后

            /* --- 跳过逗号和空白，准备解析下一个键值对 --- */
            // 例如: "username":"alice","password":"123"
            //                         ^ 跳过这个逗号
            while (pos < json.length() && (json[pos] == ',' || json[pos] == ' ' || json[pos] == '\n')) {
                pos++;
            }
        }

        return result;
    }

    // ★ 新增：解析URL查询参数
    // 例如: /api/users?id=123&name=alice
    // 返回: {{"id", "123"}, {"name", "alice"}}
    //
    // GET请求常用查询参数传递数据，因为GET请求没有body
    // POST请求则通常使用body传递数据（form-data或JSON格式）
    std::unordered_map<std::string, std::string> getQuery() const {
        std::unordered_map<std::string, std::string> params;

        /* ========================================
         * 步骤1: 查找问号，分离路径和查询字符串
         * 输入: /api/users?id=123&name=alice
         * 路径: /api/users
         * 查询: id=123&name=alice
         * ======================================== */
        size_t pos = path.find('?');
        if (pos == std::string::npos) return params;  // 没有查询参数

        std::string query = path.substr(pos + 1);  // 提取?后面的部分
        std::istringstream stream(query);
        std::string pair;

        /* ========================================
         * 步骤2: 按&分割，解析每个key=value对
         * 例如: id=123&name=alice
         * 第一次循环: pair = "id=123"
         * 第二次循环: pair = "name=alice"
         * ======================================== */
        while (std::getline(stream, pair, '&')) {
            size_t eq_pos = pair.find('=');  // 查找等号位置
            if (eq_pos == std::string::npos) continue;  // 没有等号，跳过

            std::string key = pair.substr(0, eq_pos);  // 等号前是key
            std::string value = pair.substr(eq_pos + 1);  // 等号后是value
            params[key] = value;
        }

        return params;
    }

    std::string getMethodString() const {
        switch (method) {
            case GET: return "GET";
            case POST: return "POST";
            // ... 其他方法 ...
            default: return "UNKNOWN";
        }
    }

    const std::string& getPath() const {
        return path;
    }

    // 其他成员函数和变量

private:
    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line);
        std::string method_str;
        iss >> method_str;
        if (method_str == "GET") method = GET;
        else if (method_str == "POST") method = POST;
        else method = UNKNOWN;

        iss >> path;
        iss >> version;
        state = HEADERS;
        return true;
    }

    bool parseHeader(const std::string& line) {
        size_t pos = line.find(": ");
        if (pos == std::string::npos) {
            return false;
        }
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 2);
        headers[key] = value;
        return true;
    }

    Method method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    ParseState state;
    std::string body;
};
