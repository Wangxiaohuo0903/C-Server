#pragma once

#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>

class HttpRequest {
public:
    enum Method {
        GET, POST, HEAD, PUT, DELETE_, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };
    enum ParseState { REQUEST_LINE, HEADERS, BODY, FINISH };

    HttpRequest()
        : method(UNKNOWN), state(REQUEST_LINE), contentLength(0) {}

    bool parse(const std::string& raw) {
        std::istringstream iss(raw);
        std::string line;

        while (state == REQUEST_LINE || state == HEADERS) {
            if (!std::getline(iss, line)) {
                return false;
            }
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (state == REQUEST_LINE) {
                if (!parseRequestLine(line)) {
                    return false;
                }
                state = HEADERS;
            } else if (state == HEADERS) {
                if (line.empty()) {
                    if (method == POST && contentLength > 0) {
                        state = BODY;
                    } else {
                        state = FINISH;
                    }
                    break;
                }
                if (!parseHeader(line)) {
                    return false;
                }
            }
        }

        if (state == BODY) {
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd == std::string::npos) return false;
            size_t bodyStart = headerEnd + 4;
            if (raw.size() < bodyStart + contentLength) return false;
            body = raw.substr(bodyStart, contentLength);
            state = FINISH;
        }
        return (state == FINISH);
    }

    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST || body.empty()) return params;
        std::istringstream stream(body);
        std::string pair;
        while (std::getline(stream, pair, '&')) {
            auto pos = pair.find('=');
            if (pos == std::string::npos) continue;
            std::string key   = pair.substr(0, pos);
            std::string value = pair.substr(pos + 1);
            params[key] = value;
        }
        return params;
    }

    // 解析 JSON 格式 body，形如 {"prompt":"Hello"}，提取 prompt 的值
    std::string parseJsonField(const std::string& field) const {
        auto pos = body.find("\"" + field + "\"");
        if (pos == std::string::npos) return "";
        pos = body.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = body.find_first_of("\"", pos + 1);
        if (pos == std::string::npos) return "";
        size_t endPos = body.find_first_of("\"", pos + 1);
        if (endPos == std::string::npos) return "";
        return body.substr(pos + 1, endPos - pos - 1);
    }

    std::string getMethodString() const {
        switch (method) {
            case GET:     return "GET";
            case POST:    return "POST";
            case HEAD:    return "HEAD";
            case PUT:     return "PUT";
            case DELETE_: return "DELETE";
            case TRACE:   return "TRACE";
            case OPTIONS: return "OPTIONS";
            case CONNECT: return "CONNECT";
            case PATCH:   return "PATCH";
            default:      return "UNKNOWN";
        }
    }

    const std::string& getPath() const    { return path; }
    const std::string& getVersion() const { return version; }
    const std::string& getBody() const    { return body; }

private:
    Method method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    ParseState state;
    size_t contentLength;
    std::string body;

    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line);
        std::string method_str;
        if (!(iss >> method_str >> path >> version)) {
            return false;
        }
        if (method_str == "GET")      method = GET;
        else if (method_str == "POST")method = POST;
        else if (method_str == "HEAD")method = HEAD;
        else if (method_str == "PUT") method = PUT;
        else if (method_str == "DELETE") method = DELETE_;
        else if (method_str == "TRACE")   method = TRACE;
        else if (method_str == "OPTIONS") method = OPTIONS;
        else if (method_str == "CONNECT") method = CONNECT;
        else if (method_str == "PATCH")   method = PATCH;
        else method = UNKNOWN;
        return true;
    }

    bool parseHeader(const std::string& line) {
        auto pos = line.find(':');
        if (pos == std::string::npos) return false;
        std::string key   = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        if (!value.empty() && value.front() == ' ') {
            value.erase(0, 1);
        }
        headers[key] = value;
        if (key == "Content-Length") {
            try {
                contentLength = std::stoul(value);
            } catch (...) {
                contentLength = 0;
            }
        }
        return true;
    }
};
