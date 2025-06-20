#pragma once  // 避免头文件被多次包含

#include <string>
#include <unordered_map>
#include <sstream>
#include <fstream>
#include <filesystem>

/*==========================================================
 * Minimal HTTP response builder
 * 最小化实现的 HTTP 响应构造器，构造 HTTP 响应报文字符串
 *=========================================================*/
class HttpResponse {
public:
    // 构造函数，默认状态码为 200 OK
    explicit HttpResponse(int code = 200)
        : statusCode(code), version("HTTP/1.1") {
        headers["Connection"] = "close"; // 默认关闭连接
    }

    // 设置状态码，如 404、500
    void setStatusCode(int c) { statusCode = c; }

    // 设置响应头，例如 Content-Type、Connection
    void setHeader(const std::string& k, const std::string& v) {
        headers[k] = v;
    }

    // 设置响应体（正文）
    void setBody(const std::string& b) { body = b; }

    /* ---------- 快捷构造器 ---------- */

    // 构造一个错误响应（如 404），正文是纯文本 msg
    static HttpResponse makeErrorResponse(int code, const std::string& msg) {
        HttpResponse r(code);
        r.setBody(msg);
        r.setHeader("Content-Type", "text/plain");
        return r;
    }

    // 构造一个 200 OK 的纯文本响应
    static HttpResponse makeOkResponse(const std::string& msg) {
        HttpResponse r(200);
        r.setBody(msg);
        r.setHeader("Content-Type", "text/plain");
        return r;
    }

    /* ---------- 发送本地文件 ---------- */

    // 构造一个读取本地文件的响应，Content-Type 自动根据扩展名设置
    static HttpResponse makeFileResponse(const std::string& file);

    /* ---------- 序列化为 HTTP 字符串 ---------- */

    // 将响应对象转为完整 HTTP 响应报文
    std::string toString() const;

private:
    int statusCode;      // HTTP 状态码
    std::string version; // HTTP 协议版本
    std::unordered_map<std::string, std::string> headers; // 响应头
    std::string body;    // 响应体

    // 根据状态码返回状态描述字符串
    static std::string statusText(int code);

    // 根据文件扩展名猜测 MIME 类型
    static std::string mimeFromExt(const std::string& f);
};

/*==================== 实现部分 ====================*/

// 状态码转字符串，例如 200 => "OK"
inline std::string HttpResponse::statusText(int c) {
    switch (c) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        default:  return "Unknown";
    }
}

// 根据文件扩展名猜测 Content-Type，例如 .html => text/html
inline std::string HttpResponse::mimeFromExt(const std::string& f) {
    auto dot = f.find_last_of('.');
    if (dot == std::string::npos) return "application/octet-stream"; // 无扩展名
    std::string ext = f.substr(dot + 1);

    if (ext == "html") return "text/html";
    if (ext == "css")  return "text/css";
    if (ext == "js")   return "application/javascript";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "json") return "application/json";

    return "application/octet-stream"; // 默认二进制流
}

// 返回指定路径的文件内容作为响应（状态码为 200 或 404）
inline HttpResponse HttpResponse::makeFileResponse(const std::string& file) {
    namespace fs = std::filesystem;
    if (!fs::exists(file)) return makeErrorResponse(404, "File Not Found");

    // 读取整个文件为字符串（二进制模式防止编码误差）
    std::ifstream fin(file, std::ios::binary);
    std::ostringstream oss;
    oss << fin.rdbuf();

    HttpResponse r(200);
    r.setBody(oss.str());
    r.setHeader("Content-Type", mimeFromExt(file));
    return r;
}

// 将响应转为 HTTP 报文格式字符串
inline std::string HttpResponse::toString() const {
    std::ostringstream oss;

    // 响应头第一行：HTTP/1.1 200 OK
    oss << version << ' ' << statusCode << ' ' << statusText(statusCode) << "\r\n";

    // 自动设置 Content-Length
    oss << "Content-Length: " << body.size() << "\r\n";

    // 若用户未设置 Content-Type，补默认值
    if (!headers.count("Content-Type"))
        oss << "Content-Type: text/plain\r\n";

    // 追加自定义 headers
    for (const auto& kv : headers) {
        if (kv.first == "Content-Length") continue; // 避免重复
        oss << kv.first << ": " << kv.second << "\r\n";
    }

    // 空行后是正文
    oss << "\r\n" << body;

    return oss.str();
}
