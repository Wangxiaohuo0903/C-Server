// Logger.h
#include <fstream>
#include <string>
#include <chrono>
#include <ctime>

enum LogLevel {
    INFO,
    WARNING,
    ERROR
};

class Logger {
public:
    static void logMessage(LogLevel level, const std::string& message) {
        std::ofstream logFile("server.log", std::ios::app);
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::string levelStr;

        switch (level) {
            case INFO: levelStr = "INFO"; break;
            case WARNING: levelStr = "WARNING"; break;
            case ERROR: levelStr = "ERROR"; break;
        }

        logFile << std::ctime(&now_c) << " [" << levelStr << "] " << message << std::endl;
        logFile.close();
    }
};

#define LOG_INFO(message) Logger::logMessage(INFO, message)
#define LOG_WARNING(message) Logger::logMessage(WARNING, message)
#define LOG_ERROR(message) Logger::logMessage(ERROR, message)
