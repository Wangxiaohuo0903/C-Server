// http_response.h
#pragma once
#include <string>
#include <unordered_map>
#include <sstream>
#include <vector>
#include <zlib.h> // 引入zlib库，用于数据压缩

// 定义HttpResponse类，用于构建HTTP响应
class HttpResponse {
public:
    // 构造函数，默认状态码为200（OK）
    HttpResponse(int code = 200) : statusCode(code) {}

    // 设置响应状态码
    void setStatusCode(int code) {
        statusCode = code;
    }

    // 设置HTTP头部字段
    void setHeader(const std::string& name, const std::string& value) {
        headers[name] = value;
    }

    // 设置响应体，并自动更新Content-Length头部以反映新的响应体长度
    void setBody(const std::string& b) {
        body = b;
        setHeader("Content-Length", std::to_string(body.length()));
    }

    // 设置连接是否保持活跃
    void setKeepAlive(bool enable) {
        if (enable) {
            headers["Connection"] = "keep-alive";
        } else {
            headers["Connection"] = "close";
        }
    }

    // 将HTTP响应转换为字符串格式，用于发送给客户端
    std::string toString() const {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << statusCode << " " << getStatusMessage() << "\r\n";

        // 遍历并添加所有设置的头部字段
        for (const auto& header : headers) {
            if (header.first != "Content-Encoding") {
                oss << header.first << ": " << header.second << "\r\n";
            }
        }

        // 如果响应体已被压缩，添加Content-Encoding头部
        if (headers.find("Content-Encoding") != headers.end()) {
            oss << "Content-Encoding: " << headers.at("Content-Encoding") << "\r\n";
        }

        oss << "\r\n"; // 头部与响应体之间的空行
        oss << body; // 添加响应体
        return oss.str();
    }

    // 静态方法，用于创建错误响应
    static HttpResponse makeErrorResponse(int code, const std::string& message) {
        HttpResponse response(code);
        response.setBody(message);
        return response;
    }

    // 静态方法，用于创建成功响应
    static HttpResponse makeOkResponse(const std::string& message) {
        HttpResponse response(200);
        response.setBody(message);
        return response;
    }

    // 判断响应体是否足够大，需要压缩
    bool shouldCompress() const {
        return body.length() > 1024; // 当响应体大于1024字节时，考虑压缩
    }

    // 压缩响应体，并更新相应的头部字段
    void compressBody() {
        if (body.empty() || !shouldCompress()) {
            return; // 如果响应体为空或不需要压缩，则直接返回
        }

        uLongf compressedDataSize = compressBound(body.length()); // 计算压缩后的最大长度
        std::vector<Bytef> compressedData(compressedDataSize); // 创建存储压缩数据的vector

        // 使用zlib的compress函数进行压缩
        if (compress(compressedData.data(), &compressedDataSize, reinterpret_cast<const Bytef*>(body.data()), body.length()) == Z_OK) {
            body = std::string(reinterpret_cast<char*>(compressedData.data()), compressedDataSize); // 更新响应体为压缩后的数据
            setHeader("Content-Encoding", "gzip"); // 设置Content-Encoding头部为gzip
            setHeader("Content-Length", std::to_string(compressedDataSize)); // 更新Content-Length头部为压缩后的长度
        } else {
            // 如果压缩失败，则可以在这里处理，例如记录日志等
        }
    }

private:
    // 根据状态码返回对应的状态消息
    std::string getStatusMessage() const {
        switch (statusCode) {
            case 200: return "OK";
            case 404: return "Not Found";
            default: return "Unknown";
        }
    }

    int statusCode; // HTTP状态码
    std::unordered_map<std::string, std::string> headers; // 存储HTTP头部字段
    std::string body; // 响应体内容
};
