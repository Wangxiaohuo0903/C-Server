#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

/* 读取磁盘文本。读不到返回空串（再让调用方决定 404 或 500） */
inline std::string readFile(const std::string& path) {
    if (!std::filesystem::exists(path)) return {};
    std::ifstream f(path, std::ios::binary);
    std::ostringstream oss;
    if (f) oss << f.rdbuf();
    return oss.str();
}
