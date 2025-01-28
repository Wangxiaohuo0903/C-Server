#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include <fstream>      // 用于文件读写
#include <filesystem>   // C++17, 用于检查文件存在、创建目录等
#include <sstream>
// Router 类负责将特定的 HTTP 请求映射到相应的处理函数
class Router {
public:
    // 定义处理函数的类型
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    // 添加路由：将 HTTP 方法和路径映射到处理函数
    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    // 根据 HTTP 请求路由到相应的处理函数
    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();
        if (routes.count(key)) {
            return routes[key](request);
        }
        // 如果没有找到匹配的路由，返回 404 Not Found 响应
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    // 设置数据库相关的路由，例如注册和登录
    void setupDatabaseRoutes(Database& db) {
        // 注册路由
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();  // 解析表单数据
            std::string username = params["username"];
            std::string password = params["password"];
            // 调用数据库方法进行用户注册
            if (db.registerUser(username, password)) {
                return HttpResponse::makeOkResponse("Register Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        // 登录路由
        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();  // 解析表单数据
            std::string username = params["username"];
            std::string password = params["password"];
            // 调用数据库方法进行用户登录
            if (db.loginUser(username, password)) {
                return HttpResponse::makeOkResponse("Login Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Login Failed!");
            }
        });
    }
    void setupFileRoutes(const std::string& uploadDir = "uploads") {
        // 确保上传目录存在，不存在则创建
        std::filesystem::create_directories(uploadDir);

        // 路由1: 文件上传
        addRoute("POST", "/upload", [this, uploadDir](const HttpRequest& req) {
            if (req.getMethodString() != "POST") {
                return HttpResponse::makeErrorResponse(405, "Method Not Allowed");
            }
            // 解析POST表单数据：filename, filedata
            auto params = req.parseFormBody();
            if (params.find("filename") == params.end() ||
                params.find("filedata") == params.end()) 
            {
                return HttpResponse::makeErrorResponse(400, "Missing filename or filedata");
            }

            std::string filename = params["filename"];
            std::string filedata = params["filedata"];

            // 构造存储路径
            std::string filepath = uploadDir + "/" + filename;

            // 将 filedata 写入到指定文件
            // 注意：这里是直接把文本写进去，若有二进制需另行处理
            std::ofstream ofs(filepath, std::ios::binary);
            if (!ofs.is_open()) {
                return HttpResponse::makeErrorResponse(500, "Failed to open file on server");
            }
            ofs << filedata;
            ofs.close();

            return HttpResponse::makeOkResponse("Upload Success: " + filename);
        });

        // 路由2: 文件下载
        // 形式：GET /download?filename=xxxx
      addRoute("GET", "/download", [this, uploadDir](const HttpRequest& req) {
    // 1) 先做简单的method检查
    if (req.getMethodString() != "GET") {
        LOG_WARNING("Download route called with non-GET method");
        return HttpResponse::makeErrorResponse(405, "Method Not Allowed");
    }

    // 2) 取出query字符串 (如 "filename=HttpServer.h")
    std::string queryPart = req.getQuery();
    if (queryPart.empty()) {
        LOG_WARNING("No query parameter found in download request");
        return HttpResponse::makeErrorResponse(400, "No query parameter found");
    }
    LOG_INFO("Download query string: %s", queryPart.c_str());

    // 3) 手动解析 "filename=xxx"
    std::size_t eqPos = queryPart.find('=');
    if (eqPos == std::string::npos) {
        LOG_WARNING("Invalid query format: %s", queryPart.c_str());
        return HttpResponse::makeErrorResponse(400, "No valid filename parameter");
    }
    std::string key = queryPart.substr(0, eqPos);      // "filename"
    std::string value = queryPart.substr(eqPos + 1);   // "HttpServer.h"

    if (key != "filename" || value.empty()) {
        LOG_WARNING("Invalid parameter. key=%s value=%s", key.c_str(), value.c_str());
        return HttpResponse::makeErrorResponse(400, "Invalid parameter");
    }
    LOG_INFO("Download requested for file: %s", value.c_str());

    // 4) 拼接本地文件路径
    std::string filename = value;
    std::string filepath = uploadDir + "/" + filename;

    // 5) 检查文件存在性
    if (!std::filesystem::exists(filepath)) {
        LOG_WARNING("File not found: %s", filepath.c_str());
        return HttpResponse::makeErrorResponse(404, "File Not Found");
    }

    // 6) 读取文件并构建响应
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs.is_open()) {
        LOG_ERROR("Failed to open file: %s", filepath.c_str());
        return HttpResponse::makeErrorResponse(500, "Failed to open file");
    }
    std::ostringstream buf;
    buf << ifs.rdbuf();
    std::string fileContent = buf.str();
    ifs.close();

    HttpResponse response(200);
    response.setHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    response.setHeader("Content-Type", "application/octet-stream");
    response.setBody(fileContent);

    LOG_INFO("Download success: %s (size=%zu bytes)", filepath.c_str(), fileContent.size());
    return response;
});

        //路由3 查看文件
        addRoute("GET", "/files", [uploadDir](const HttpRequest& req) {
    // 遍历目录，收集所有常规文件的文件名
    std::vector<std::string> filenames;
    for (const auto& entry : std::filesystem::directory_iterator(uploadDir)) {
        if (entry.is_regular_file()) {
            // 只取最后的文件名，不要带路径
            filenames.push_back(entry.path().filename().string());
        }
    }

    // 简单将其拼成 JSON 数组
    // 这里手动组装，或可使用第三方 JSON 库如 nlohmann/json
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < filenames.size(); i++) {
        oss << "\"" << filenames[i] << "\"";
        if (i + 1 < filenames.size()) {
            oss << ",";
        }
    }
    oss << "]";

    HttpResponse resp(200);
    resp.setHeader("Content-Type", "application/json");
    resp.setBody(oss.str());
    return resp;
});

//路由4
addRoute("GET", "/index", [this](const HttpRequest& req) {
        // 假设 index.html 与可执行程序位于同一目录
        const std::string filename = "UI/index.html";

        std::ifstream ifs(filename);
        if (!ifs.is_open()) {
            // 如果文件没找到或读取失败，返回 404
            return HttpResponse::makeErrorResponse(404, "index.html Not Found");
        }

        // 读取文件全部内容
        std::stringstream buffer;
        buffer << ifs.rdbuf();
        ifs.close();

        // 构造响应
        HttpResponse resp(200);
        // 告诉浏览器这是一个 HTML 文档
        resp.setHeader("Content-Type", "text/html; charset=UTF-8");
        resp.setBody(buffer.str());
        return resp;
    });
    }
private:
    std::unordered_map<std::string, HandlerFunc> routes;  // 存储路由映射
};
