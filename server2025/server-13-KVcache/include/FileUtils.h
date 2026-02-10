/*==========================================================
 * FileUtils.h - 文件操作工具类
 *
 * 【初学者指南】Server-12 新增的文件工具
 *
 * 功能：
 * - 读取静态文件（HTML、JS、CSS等）
 * - 为Web界面提供文件服务
 *
 * 使用场景：
 * - 加载聊天界面的 HTML 文件
 * - 读取配置文件
 * - 提供静态资源服务
 *
 * 【为什么需要这个工具】
 * Server-12 有 Web 界面（类似 ChatGPT），需要：
 * - 读取 multichat.html
 * - 读取 JavaScript 文件
 * - 返回给浏览器显示
 *=========================================================*/

#pragma once
#include <string>
#include <fstream>       // 文件输入输出流
#include <sstream>       // 字符串流
#include <filesystem>    // C++17 文件系统库

/**
 * @brief 从磁盘读取文本文件内容
 *
 * 【初学者笔记 - 文件读取】
 * 这是一个简单但实用的工具函数，用于读取任何文本文件
 *
 * @param path 文件路径（绝对路径或相对路径）
 *             示例：
 *             - "/app/UI/multichat.html"
 *             - "./config.json"
 *             - "data/users.txt"
 * @return 文件内容（字符串），如果文件不存在返回空字符串 ""
 *
 * 【工作流程】
 * ┌────────────────────────────────────┐
 * │ 1. 检查文件是否存在                │
 * │    std::filesystem::exists()       │
 * └────────────────────────────────────┘
 *              ↓
 *         文件存在？
 *         /        \
 *       是          否
 *       ↓           ↓
 * ┌───────────┐  返回空字符串 ""
 * │ 2. 打开文件│
 * │ (二进制模式)│
 * └───────────┘
 *       ↓
 * ┌────────────────────────────────────┐
 * │ 3. 读取所有内容到字符串流          │
 * │    oss << f.rdbuf()                │
 * │    (一次性读取，高效)              │
 * └────────────────────────────────────┘
 *       ↓
 * ┌────────────────────────────────────┐
 * │ 4. 转换为 std::string 并返回       │
 * └────────────────────────────────────┘
 *
 * 【为什么使用二进制模式】
 * std::ios::binary 的作用：
 * - 不转换换行符（保持文件原样）
 * - Windows: CRLF (\r\n) 保持不变
 * - Unix/Linux: LF (\n) 保持不变
 * - 避免文本模式的"智能"转换导致问题
 *
 * 【为什么返回空字符串而不是抛异常】
 * 设计哲学：让调用方决定如何处理
 * - 文件不存在可能是正常情况（用户删除了文件）
 * - 调用方可以检查返回值是否为空
 * - 根据业务逻辑决定返回 404 还是 500
 *
 * 【使用示例】
 * ```cpp
 * // 示例1: 读取 HTML 文件
 * std::string html = readFile("/app/UI/chat.html");
 * if (html.empty()) {
 *     // 文件不存在，返回 404
 *     return HttpResponse::makeErrorResponse(404, "Page not found");
 * }
 * return HttpResponse(200, html, "text/html");
 *
 * // 示例2: 读取配置文件
 * std::string config = readFile("./config.json");
 * if (config.empty()) {
 *     // 使用默认配置
 *     config = "{\"port\": 8080}";
 * }
 *
 * // 示例3: 读取模型配置
 * std::string model_config = readFile("model_config.txt");
 * // 处理配置...
 * ```
 *
 * 【inline 关键字】
 * 为什么使用 inline：
 * - 这是一个简短的函数，适合内联
 * - 内联可以避免函数调用开销（微小性能提升）
 * - 头文件中定义函数必须用 inline，否则多次包含会重复定义
 *
 * 【性能特性】
 * - 时间复杂度: O(n)，n = 文件大小
 * - 空间复杂度: O(n)，返回的字符串
 * - 适合小到中型文件（几KB到几MB）
 * - 不适合超大文件（会占用太多内存）
 *
 * 【线程安全】
 * - 完全线程安全
 * - 每次调用都创建独立的 ifstream 和 ostringstream
 * - 没有共享状态
 */
inline std::string readFile(const std::string& path) {
    // 步骤1: 检查文件是否存在
    /**
     * std::filesystem::exists() 返回 bool:
     * - true: 文件或目录存在
     * - false: 不存在或无权限访问
     *
     * 为什么要先检查：
     * - 避免打开不存在的文件（会失败）
     * - 提前返回，节省时间
     * - 清晰的错误处理逻辑
     */
    if (!std::filesystem::exists(path)) {
        return {};  // {} 等价于 std::string()，返回空字符串
    }

    // 步骤2: 打开文件（二进制模式）
    /**
     * std::ifstream: Input File Stream，文件输入流
     * std::ios::binary: 二进制模式标志
     *
     * 为什么要二进制模式：
     * - 保持文件内容完全不变
     * - 不转换换行符 \r\n ↔ \n
     * - 适合读取任何类型的文本文件
     */
    std::ifstream f(path, std::ios::binary);

    // 步骤3: 创建字符串流，读取所有内容
    /**
     * std::ostringstream: Output String Stream，字符串输出流
     * 可以像 std::cout 一样使用，但输出到字符串而不是控制台
     */
    std::ostringstream oss;

    // 步骤4: 读取文件内容到字符串流
    /**
     * f.rdbuf(): 返回文件流的缓冲区
     * oss << f.rdbuf(): 将整个缓冲区内容输出到字符串流
     *
     * 这是读取整个文件的高效方法：
     * - 一次性读取（不需要循环）
     * - 利用流的缓冲机制（底层优化）
     * - 代码简洁
     *
     * if (f) 检查流是否成功打开：
     * - true: 文件打开成功
     * - false: 打开失败（权限问题、文件损坏等）
     */
    if (f) {
        oss << f.rdbuf();  // 读取整个文件到 oss
    }

    // 步骤5: 返回字符串
    /**
     * oss.str(): 获取字符串流中的内容，返回 std::string
     *
     * 如果文件打开失败，oss 是空的，str() 返回空字符串
     */
    return oss.str();
}

/*==========================================================
 * 可能的扩展功能（暂未实现）
 *=========================================================*/

/**
 * 以下是一些可能有用的扩展函数（供参考）：
 *
 * // 写入文件
 * inline bool writeFile(const std::string& path, const std::string& content);
 *
 * // 读取二进制文件
 * inline std::vector<uint8_t> readBinaryFile(const std::string& path);
 *
 * // 检查文件是否存在
 * inline bool fileExists(const std::string& path);
 *
 * // 获取文件大小
 * inline size_t getFileSize(const std::string& path);
 *
 * // 获取文件扩展名
 * inline std::string getFileExtension(const std::string& path);
 *
 * // 读取文件的前N行
 * inline std::string readFirstLines(const std::string& path, int n);
 */
