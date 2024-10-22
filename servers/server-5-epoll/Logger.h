#include <fstream>
#include <string>
#include <chrono>
#include <ctime>
#include <cstdarg> // 引入处理可变参数的头文件

// 日志级别枚举，用于区分不同类型的日志
enum LogLevel {
    INFO,
    WARNING,
    ERROR
};

// Logger类，用于执行日志记录操作
class Logger {
public:
    // logMessage静态成员函数，用于记录日志信息
    // 参数包括日志级别、格式化字符串以及可变参数列表
    static void logMessage(LogLevel level, const char* format, ...) {
        // 打开日志文件，以追加模式写入
        std::ofstream logFile("server.log", std::ios::app);
        
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        
        // 根据日志级别确定日志级别字符串
        std::string levelStr;
        switch (level) {
            case INFO: levelStr = "INFO"; break;
            case WARNING: levelStr = "WARNING"; break;
            case ERROR: levelStr = "ERROR"; break;
        }

        // 使用可变参数处理日志信息的格式化
        va_list args;
        va_start(args, format);
        char buffer[2048];
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // 将时间戳、日志级别和格式化后的日志信息写入日志文件
        logFile << std::ctime(&now_c) << " [" << levelStr << "] " << buffer << std::endl;

        // 关闭日志文件
        logFile.close();
    }
};

// 定义宏以简化日志记录操作，提供INFO、WARNING、ERROR三种日志级别的宏
#define LOG_INFO(...) Logger::logMessage(INFO, __VA_ARGS__)
#define LOG_WARNING(...) Logger::logMessage(WARNING, __VA_ARGS__)
#define LOG_ERROR(...) Logger::logMessage(ERROR, __VA_ARGS__)
