#ifndef LOG_H
#define LOG_H

#include <iostream>
#include <string>
#include <fstream>

// 定义一个简单的日志系统
class Logger {
public:
    Logger() {
        log_file.open("server.log", std::ios::app); // 打开日志文件
    }

    ~Logger() {
        if (log_file.is_open()) {
            log_file.close(); // 关闭日志文件
        }
    }

    void log(const std::string& level, const std::string& message) {
        log_file << level << ": " << message << std::endl;
        std::cout << level << ": " << message << std::endl; // 同时输出到控制台
    }

private:
    std::ofstream log_file;
};

// 全局日志对象
extern Logger global_logger;

// 日志宏
#define LOG_INFO(message) global_logger.log("INFO", message)
#define LOG_ERROR(message) global_logger.log("ERROR", message)
#define LOG_WARN(message) global_logger.log("WARN", message)

#endif // LOG_H
