#ifndef DATABASE_H
#define DATABASE_H

#include <hiredis/hiredis.h>
#include <string>

class Database {
private:
    redisContext* context;

public:
    // 构造函数，连接到 Redis 节点
    Database(const std::string& host, int port) {
        struct timeval timeout = { 1, 500000 }; // 1.5 seconds
        context = redisConnectWithTimeout(host.c_str(), port, timeout);
        if (context == NULL || context->err) {
            if (context) {
                throw std::runtime_error("Redis connection error: " + std::string(context->errstr));
            } else {
                throw std::runtime_error("Redis connection error: can't allocate redis context");
            }
        }
    }

    // 析构函数，关闭连接
    ~Database() {
        if (context != NULL) {
            redisFree(context);
        }
    }

    // 用户注册函数
    bool registerUser(const std::string& username, const std::string& password) {
        std::string key = "user:" + username;
        redisReply* reply = (redisReply*)redisCommand(context, "SETNX %s %s", key.c_str(), password.c_str());
        bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
        freeReplyObject(reply);
        return success;
    }

    // 用户登录函数
    bool loginUser(const std::string& username, const std::string& password) {
        std::string key = "user:" + username;
        redisReply* reply = (redisReply*)redisCommand(context, "GET %s", key.c_str());
        bool success = false;
        if (reply && reply->type == REDIS_REPLY_STRING) {
            success = (password == reply->str);
        }
        freeReplyObject(reply);
        return success;
    }
};

#endif // DATABASE_H
