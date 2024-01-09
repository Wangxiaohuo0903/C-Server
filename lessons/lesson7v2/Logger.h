// Logger.h

// 包含必要的头文件
#include <fstream>          // 用于文件操作
#include <string>           // 用于字符串操作
#include <chrono>           // 用于时间操作
#include <ctime>            // 用于时间格式化

// 枚举定义不同的日志级别
enum LogLevel {
    INFO,       // 信息
    WARNING,    // 警告
    ERROR       // 错误
};

// 日志记录器类
class Logger {
public:
    // 静态方法用于记录日志消息
    static void logMessage(LogLevel level, const std::string& message) {
        // 打开日志文件，以追加方式写入
        std::ofstream logFile("server.log", std::ios::app);
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        
        // 将日志级别转换为字符串表示
        std::string levelStr;

        switch (level) {
            case INFO: levelStr = "INFO"; break;
            case WARNING: levelStr = "WARNING"; break;
            case ERROR: levelStr = "ERROR"; break;
        }

        // 将时间、日志级别、消息写入日志文件
        logFile << std::ctime(&now_c) << " [" << levelStr << "] " << message << std::endl;

        // 关闭日志文件
        logFile.close();
    }
};

// 定义宏以简化日志记录操作
#define LOG_INFO(message) Logger::logMessage(INFO, message)
#define LOG_WARNING(message) Logger::logMessage(WARNING, message)
#define LOG_ERROR(message) Logger::logMessage(ERROR, message)
