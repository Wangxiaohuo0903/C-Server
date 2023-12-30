#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Database.h"
#include <functional>
#include <unordered_map>
#include <future>
#include <sys/stat.h>  // 包含 mkdir 函数的声明
#include <cerrno>      // 包含 errno 的声明


class Router {
public:
    using HandlerFunc = std::function<HttpResponse(const HttpRequest&)>;

    void addRoute(const std::string& method, const std::string& path, HandlerFunc handler) {
        routes[method + "|" + path] = handler;
    }

    HttpResponse routeRequest(const HttpRequest& request) {
        std::string key = request.getMethodString() + "|" + request.getPath();
        if (routes.count(key)) {
            return routes[key](request);
        }
        return HttpResponse::makeErrorResponse(404, "Not Found");
    }

    void setupDatabaseRoutes(Database& db) {
         // 注册路由
        addRoute("POST", "/register", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody(); 
            std::string username = params["username"];
            std::string password = params["password"];
            // 异步调用数据库注册方法
            auto future = db.registerUserAsync(username, password);
            future.wait(); // 等待异步操作完成
            if (future.get()) {
                return HttpResponse::makeOkResponse("Register Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Register Failed!");
            }
        });

        // 登录路由
        addRoute("POST", "/login", [&db](const HttpRequest& req) {
            auto params = req.parseFormBody();
            std::string username = params["username"];
            std::string password = params["password"];
            // 异步调用数据库登录方法
            auto future = db.loginUserAsync(username, password);
            future.wait(); // 等待异步操作完成
            if (future.get()) {
                return HttpResponse::makeOkResponse("Login Success!");
            } else {
                return HttpResponse::makeErrorResponse(400, "Login Failed!");
            }
        });
    }

void setupImageRoutes(Database& db) {
        // 图片上传路由
        addRoute("POST", "/upload", [&db](const HttpRequest& req) {
            // 获取表单字段
            std::string fileContent = req.getFileContent("file");
            std::string fileName = req.getFileName("file");  // 使用新方法获取文件名
            std::string description = req.getFormField("description");


            // 检查并创建目录
            std::string dirPath = "images/";
            if (mkdir(dirPath.c_str(), 0777) == -1 && errno != EEXIST) {
                LOG_ERROR("Failed to create directory: " + dirPath);
                return HttpResponse::makeErrorResponse(500, "Internal Server Error: Unable to create directory");
            }

            // 保存文件到服务器的某个路径
            std::string filePath = dirPath + fileName;
            std::ofstream file(filePath, std::ios::binary);
            if (!file.is_open()) {
                LOG_ERROR("Failed to open file for writing: " + filePath);
                return HttpResponse::makeErrorResponse(500, "Internal Server Error: Unable to save file");
            }
            file.write(fileContent.c_str(), fileContent.size());
            file.close();
            LOG_INFO("File saved successfully: " + filePath);

            // 将图片信息存入数据库
            if (!db.storeImage(fileName, filePath, description)) {
                LOG_ERROR("Failed to store image info in database");
                return HttpResponse::makeErrorResponse(500, "Internal Server Error: Unable to store image info");
            }

            LOG_INFO("Image uploaded successfully: " + fileName);
            return HttpResponse::makeOkResponse("Image uploaded successfully");
        });


        // 获取图片列表路由
        addRoute("GET", "/images", [&db](const HttpRequest& req) {
            std::vector<std::string> imageList = db.getImageList();
            std::stringstream ss;
            ss << "[";
            for (size_t i = 0; i < imageList.size(); ++i) {
                ss << "\"" << imageList[i] << "\"";
                if (i < imageList.size() - 1) {
                    ss << ", ";
                }
            }
            ss << "]";

            HttpResponse response;
            response.setStatusCode(200);
            response.setHeader("Content-Type", "application/json");
            response.setBody(ss.str());
            return response;
        });
    }

private:
    std::unordered_map<std::string, HandlerFunc> routes;
};
