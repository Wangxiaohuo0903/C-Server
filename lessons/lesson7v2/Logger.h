#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <cstdarg> // 用于处理可变参数

enum LogLevel {
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static void logMessage(LogLevel level, const char* format, ...) {
        std::ofstream logFile("server.log", std::ios::app);
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        
        std::string levelStr;
        switch (level) {
            case INFO: levelStr = "INFO"; break;
            case WARNING: levelStr = "WARNING"; break;
            case ERROR: levelStr = "ERROR"; break;
        }

        // 处理可变参数
        va_list args;
        va_start(args, format);
        char buffer[2048];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 将时间、日志级别、消息写入日志文件
        logFile << std::ctime(&now_c) << " [" << levelStr << "] " << buffer << std::endl;

        // 关闭日志文件
        logFile.close();
    }
};

// 定义宏以简化日志记录操作
#define LOG_INFO(...) Logger::logMessage(INFO, __VA_ARGS__)
#define LOG_WARNING(...) Logger::logMessage(WARNING, __VA_ARGS__)
#define LOG_ERROR(...) Logger::logMessage(ERROR, __VA_ARGS__)
