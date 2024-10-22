#pragma once
#include <string>
#include <unordered_map>
#include <sstream>

// 定义HttpRequest类处理HTTP请求
class HttpRequest {
public:
    // 定义HTTP请求方法枚举
    enum Method {
        GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH, UNKNOWN
    };

    // 解析状态枚举，用于跟踪请求解析进度
    enum ParseState {
        REQUEST_LINE, HEADERS, BODY, FINISH
    };

    // 构造函数初始化为未知方法和请求行状态
    HttpRequest() : method(UNKNOWN), state(REQUEST_LINE) {}

    // 解析HTTP请求字符串
    bool parse(const std::string& request) {
        std::istringstream iss(request); // 使用istringstream解析请求字符串
        std::string line; // 用于存储每行字符串
        bool result = true; // 解析结果标志

        // 解析请求行
        if (std::getline(iss, line)) {
            result = parseRequestLine(line); // 解析请求行并检查是否成功
        }

        // 解析头部，直到遇到空行
        while (result && std::getline(iss, line) && !line.empty() && line != "\r") {
            result = parseHeader(line); // 解析每个头部行
        }

        // 如果是POST请求且头部解析成功，处理请求体
        if (method == POST && result) {
            // 检查是否为multipart/form-data类型
            auto contentType = getHeader("Content-Type");
            if (contentType.find("multipart/form-data") != std::string::npos) {
                std::string boundary = getBoundary(contentType); // 获取边界标识
                // 使用'\0'作为分隔符从iss中读取剩余部分作为body
                std::getline(iss, line, '\0');
                body = line; // 存储请求体
                parseMultipartFormData(boundary); // 解析multipart/form-data请求体
            } else {
                // 处理其他类型的POST请求
                std::getline(iss, body, '\0'); // 直接读取剩余部分作为body
            }
        }

        return result; // 返回解析结果
    }

    // 解析表单编码的请求体，用于非multipart/form-data类型的POST请求
    std::unordered_map<std::string, std::string> parseFormBody() const {
        std::unordered_map<std::string, std::string> params;
        if (method != POST) return params; // 如果不是POST请求，直接返回

        std::istringstream stream(body); // 使用istringstream解析body
        std::string pair; // 存储键值对字符串

        // 分割键值对
        while (std::getline(stream, pair, '&')) {
            std::size_t pos = pair.find('=');
            if (pos == std::string::npos) continue; // 如果没有找到'='，跳过
            std::string key = pair.substr(0, pos); // 提取键
            std::string value = pair.substr(pos + 1); // 提取值
            params[key] = value; // 存入结果映射
        }

        return params; // 返回解析后的键值对映射
    }

    // 获取请求方法的字符串表示
    std::string getMethodString() const {
        switch (method) {
            case GET: return "GET";
            case POST: return "POST";
            default: return "UNKNOWN";
        }
    }

    // 获取请求路径
    const std::string& getPath() const {
        return path;
    }

    // 根据键获取请求头部的值
    std::string getHeader(const std::string& key) const {
        auto it = headers.find(key);
        if (it != headers.end()) {
            return it->second; // 如果找到，返回对应的值
        }
        return ""; // 如果未找到，返回空字符串
    }

    // 获取表单字段的值
    std::string getFormField(const std::string& fieldName) const {
        auto it = formFields.find(fieldName);
        if (it != formFields.end()) {
            return it->second; // 如果找到，返回字段值
        }
        return ""; // 如果未找到，返回空字符串
    }

    // 获取文件内容
    std::string getFileContent(const std::string& fieldName) const {
        auto it = fileContents.find(fieldName);
        if (it != fileContents.end()) {
            return it->second; // 如果找到，返回文件内容
        }
        return ""; // 如果未找到，返回空字符串
    }

    // 获取文件名
    std::string getFileName(const std::string& fieldName) const {
        auto it = fileNames.find(fieldName);
        if (it != fileNames.end()) {
            return it->second; // 如果找到，返回文件名
        }
        return ""; // 如果未找到，返回空字符串
    }

private:
    // 解析请求行
    bool parseRequestLine(const std::string& line) {
        std::istringstream iss(line); // 使用istringstream解析请求行
        std::string methodStr; // 存储方法字符串
        iss >> methodStr; // 读取方法
        if (methodStr == "GET") method = GET;
        else if (methodStr == "POST") method = POST;
        else method = UNKNOWN; // 如果不是GET或POST，标记为未知
        iss >> path; // 读取路径
        iss >> version; // 读取HTTP版本
        state = HEADERS; // 更新状态为正在解析头部
        return true; // 总是返回true，简化处理
    }

    // 解析头部行
    bool parseHeader(const std::string& line) {
        size_t pos = line.find(": ");
        if (pos == std::string::npos) return false; // 如果没有找到': '，返回失败
        std::string key = line.substr(0, pos); // 提取键
        std::string value = line.substr(pos + 2); // 提取值
        headers[key] = value; // 存入头部映射
        return true; // 返回成功
    }

    // 解析multipart/form-data请求体
    void parseMultipartFormData(const std::string& boundary) {
        size_t pos = 0;
        size_t endPos = body.find(boundary, pos); // 查找边界标识

        // 循环解析每个部分
        while (endPos != std::string::npos) {
            std::string part = body.substr(pos, endPos - pos - 2); // 提取部分内容
            parsePart(part); // 解析单个部分
            pos = endPos + boundary.length() + 2; // 更新位置
            endPos = body.find(boundary, pos); // 查找下一个边界标识
        }
    }

    // 解析请求体的单个部分
    void parsePart(const std::string& part) {
        std::istringstream iss(part); // 使用istringstream解析部分内容
        std::string line; // 存储每行字符串
        std::string name; // 存储字段名
        std::string filename; // 存储文件名
        bool isFile = false; // 标记是否为文件

        // 逐行解析部分头部
        while (std::getline(iss, line) && line != "\r") {
            size_t pos = line.find(": ");
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos); // 提取键
                std::string value = line.substr(pos + 2); // 提取值
                if (key == "Content-Disposition") {
                    // 解析Content-Disposition头部，获取字段名和文件名
                    size_t namePos = value.find("name=\"");
                    size_t filenamePos = value.find("filename=\"");
                    if (namePos != std::string::npos) {
                        namePos += 6; // 跳过"name=\""
                        size_t nameEnd = value.find("\"", namePos);
                        name = value.substr(namePos, nameEnd - namePos); // 提取字段名
                    }
                    if (filenamePos != std::string::npos) {
                        filenamePos += 10; // 跳过"filename=\""
                        size_t filenameEnd = value.find("\"", filenamePos);
                        filename = value.substr(filenamePos, filenameEnd - filenamePos); // 提取文件名
                        isFile = true; // 标记为文件
                    }
                }
            }
        }

        // 提取部分内容，去除头部后的第一个空行
        std::string content = part.substr(iss.tellg());
        content.erase(0, content.find_first_not_of("\r\n")); // 去除前导的换行符
        if (isFile) {
            fileContents[name] = content; // 存储文件内容
            fileNames[name] = filename; // 存储文件名
        } else {
            formFields[name] = content; // 存储表单字段值
        }
    }

    // 从Content-Type头部获取边界标识
    std::string getBoundary(const std::string& contentType) const {
        size_t pos = contentType.find("boundary=");
        if (pos != std::string::npos) {
            pos += 9; // 跳过"boundary="
            return "--" + contentType.substr(pos); // 返回边界标识，前面加上"--"
        }
        return ""; // 如果没有找到边界标识，返回空字符串
    }

    Method method; // 请求方法
    std::string path; // 请求路径
    std::string version; // HTTP版本
    std::unordered_map<std::string, std::string> headers; // 存储头部映射
    ParseState state; // 当前解析状态
    std::string body; // 请求体内容

    // 新增的成员变量，用于存储文件名、表单字段和文件内容
    std::unordered_map<std::string, std::string> fileNames;
    std::unordered_map<std::string, std::string> formFields;
    std::unordered_map<std::string, std::string> fileContents;
};
