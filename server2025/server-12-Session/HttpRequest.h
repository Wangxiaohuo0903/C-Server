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
    std::unordered_map<std::string, std::string> parseJson() const {
        std::unordered_map<std::string, std::string> result;
        std::string json = body;

        // 去除首尾的空白字符和大括号
        size_t start = json.find('{');
        size_t end = json.rfind('}');
        if (start == std::string::npos || end == std::string::npos) {
            return result; // 空的JSON
        }
        json = json.substr(start + 1, end - start - 1);

        // 解析键值对
        size_t pos = 0;
        while (pos < json.length()) {
            // 跳过空白字符
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) {
                pos++;
            }
            if (pos >= json.length()) break;

            // 查找键的开始引号
            if (json[pos] != '\"') {
                pos++;
                continue;
            }
            size_t key_start = pos + 1;
            size_t key_end = json.find('\"', key_start);
            if (key_end == std::string::npos) break;
            std::string key = json.substr(key_start, key_end - key_start);

            // 查找冒号
            pos = json.find(':', key_end);
            if (pos == std::string::npos) break;
            pos++;

            // 跳过空白
            while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t')) {
                pos++;
            }

            // 查找值的开始引号
            if (pos >= json.length() || json[pos] != '\"') {
                pos++;
                continue;
            }
            size_t val_start = pos + 1;
            size_t val_end = json.find('\"', val_start);
            if (val_end == std::string::npos) break;
            std::string value = json.substr(val_start, val_end - val_start);

            result[key] = value;
            pos = val_end + 1;

            // 跳过逗号
            while (pos < json.length() && (json[pos] == ',' || json[pos] == ' ' || json[pos] == '\n')) {
                pos++;
            }
        }

        return result;
    }

    // ★ 新增：解析URL查询参数
    // 例如: /api/users?id=123&name=alice
    std::unordered_map<std::string, std::string> getQuery() const {
        std::unordered_map<std::string, std::string> params;
        size_t pos = path.find('?');
        if (pos == std::string::npos) return params;

        std::string query = path.substr(pos + 1);
        std::istringstream stream(query);
        std::string pair;

        while (std::getline(stream, pair, '&')) {
            size_t eq_pos = pair.find('=');
            if (eq_pos == std::string::npos) continue;
            std::string key = pair.substr(0, eq_pos);
            std::string value = pair.substr(eq_pos + 1);
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
