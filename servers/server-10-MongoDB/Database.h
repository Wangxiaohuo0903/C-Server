#ifndef DATABASE_H
#define DATABASE_H

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <string>
#include <future>
#include <iostream> // 添加标准输出库

class Database {
private:
    mongocxx::instance instance{}; // 全局实例
    mongocxx::client client;       // MongoDB 客户端
    mongocxx::database db;         // 数据库
    std::mutex dbMutex;

public:
    // 构造函数
    Database(const std::string& uri) {
        LOG_INFO("Connecting to MongoDB");
        std::cout << "Connecting to MongoDB at: " << uri << std::endl;
        client = mongocxx::client{mongocxx::uri{uri}};
        db = client["userdb"]; // 使用 userdb 数据库
    }


// 注册用户
bool registerUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> guard(dbMutex); // 锁定互斥锁
    LOG_INFO("Trying to register user: %s", username.c_str());
    auto collection = db["users"];

    // 检查用户名是否已存在
    bsoncxx::builder::stream::document check_document{};
    check_document << "username" << username;
    auto check_result = collection.find_one(check_document.view());
    if (check_result) {
        // 如果找到了用户，说明用户名已存在，返回false
        LOG_INFO("Username already exists: %s", username.c_str());
        return false;
    }

    // 用户名不存在，可以注册
    bsoncxx::builder::stream::document document{};
    document << "username" << username << "password" << password;
    auto result = collection.insert_one(document.view());

    if (result) {
        LOG_INFO("User registered successfully: %s", username.c_str());
        return true; // 注册成功
    } else {
        LOG_ERROR("Failed to register user: %s", username.c_str());
        return false; // 注册失败
    }
}


    // 登录用户
    // 登录用户
    bool loginUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> guard(dbMutex); // 锁定互斥锁
        LOG_INFO("Trying to login user: %s", username.c_str());
        auto collection = db["users"];

        // 检查用户名是否存在
        bsoncxx::builder::stream::document document{};
        document << "username" << username;
        auto cursor = collection.find(document.view());

        if (cursor.begin() == cursor.end()) {
            // 如果没有找到用户，说明用户名不存在
            LOG_INFO("Username does not exist: %s", username.c_str());
            return false; // 登录失败
        }

        for (auto&& doc : cursor) {
            std::string stored_password = doc["password"].get_utf8().value.to_string();
            if (password == stored_password) {
                LOG_INFO("User logged in successfully: %s", username.c_str());
                return true; // 登录成功
            }
        }

        LOG_INFO("Password incorrect for user: %s", username.c_str());
        return false; // 密码错误
    }

     // 存储图片信息
    // 定义一个函数用于存储图片信息，输入参数包括图片名称、图片路径以及描述信息
    bool storeImage(const std::string& imageName, const std::string& imagePath, const std::string& description) {
        // 使用std::lock_guard保证在操作数据库时线程安全，自动锁定并解锁互斥锁dbMutex
        std::lock_guard<std::mutex> guard(dbMutex);

        try {
            // 创建一个BSON文档构建器，将图片的相关信息（名称、路径和描述）添加到文档中
            bsoncxx::builder::stream::document document{};
            document << "name" << imageName   // 图片的名称
                    << "path" << imagePath   // 图片的文件系统路径
                    << "description" << description;  // 图片的描述信息

            // 获取指向“images”集合的引用
            auto collection = db["images"];

            // 尝试将构建好的文档插入到数据库的“images”集合中
            bsoncxx::stdx::optional<mongocxx::result::insert_one> result = collection.insert_one(document.view());

            // 检查插入是否成功
            if (result) {  // 插入成功
                // 记录日志，表明图片信息已成功存入数据库
                LOG_INFO("Image information stored successfully: %s", imageName.c_str());
                return true;
            } else {      // 插入失败
                // 记录错误日志，表示未能将图片信息保存至数据库
                LOG_ERROR("Failed to store image information in database for: %s", imageName.c_str());
                return false;
            }
        } catch (const std::exception& e) { // 捕获处理在插入过程中可能抛出的任何异常
            // 记录异常详细信息的日志
            LOG_ERROR("Exception while storing image information for %s: %s", imageName.c_str(), e.what());
            // 因为发生了异常，所以返回false，表示存储失败
            return false;
        }
    }


    // 获取图片列表
    std::vector<std::string> getImageList() {
        std::vector<std::string> images;
        auto collection = db["images"];
        auto cursor = collection.find({});
        for (auto&& doc : cursor) {
            images.push_back(doc["path"].get_utf8().value.to_string());
        }
        return images;
    }
};

#endif // DATABASE_H
