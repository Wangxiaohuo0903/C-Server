/*==========================================================
 * Server-13: CMake构建 + 模块化重构
 *
 * 新增功能：
 * 1. CMake构建系统 - 自动化编译
 * 2. 模块化目录结构 - include/src分离
 * 3. 第三方库管理 - llama.cpp子模块
 *
 * 继承自Server-12：
 * - SessionManager会话管理
 * - 多轮对话支持
 * - SimpleInference推理引擎
 * - JSON + RESTful API
 *=========================================================*/

#include "http/HttpServer.h"
#include "database/Database.h"
#include "inference/SimpleInference.h"
#include "inference/SessionManager.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    int port = 8080; // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }

    std::cout << "=== Server-12: 会话管理 + 多轮对话 ===\n";
    std::cout << "Port: " << port << "\n\n";

    // ★ Server-12新增：初始化会话管理器
    std::cout << "[1/4] Initializing session manager...\n";
    SessionManager sessionMgr;
    std::cout << "[SessionManager] Ready for multi-turn conversations\n\n";

    // Server-11：初始化推理引擎
    std::cout << "[2/4] Initializing inference engine...\n";
    SimpleInference inference;

    // 从环境变量或使用默认路径加载模型
    const char* model_path_env = std::getenv("MODEL_PATH");
    std::string model_path = model_path_env ? model_path_env : "../models/tinyllama-q4.gguf";

    std::cout << "[3/4] Loading model: " << model_path << "\n";
    if (!inference.loadModel(model_path)) {
        std::cerr << "\n❌ Failed to load model!\n";
        std::cerr << "Please set MODEL_PATH environment variable or place model at: " << model_path << "\n";
        std::cerr << "Example: export MODEL_PATH=/path/to/your/model.gguf\n\n";
        return 1;
    }

    std::cout << "[4/4] Starting HTTP server...\n\n";

    std::cout << "💬 多轮对话接口（★ Server-12新增）:\n";
    std::cout << "  POST   /chat/create           - 创建新会话\n";
    std::cout << "                                  Response: {\"success\":true, \"session_id\":\"sess_xxx\"}\n";
    std::cout << "  POST   /chat                  - 多轮对话\n";
    std::cout << "                                  Request:  {\"session_id\":\"sess_xxx\", \"message\":\"...\"}\n";
    std::cout << "                                  Response: {\"success\":true, \"response\":\"...\", \"session_id\":\"sess_xxx\"}\n";
    std::cout << "  GET    /chat/history?session_id=xxx - 获取会话历史\n";
    std::cout << "  DELETE /chat/delete           - 删除会话\n";
    std::cout << "\n📡 AI推理接口（继承自Server-11）:\n";
    std::cout << "  POST /infer-simple            - 单轮推理（无会话）\n";
    std::cout << "\n📋 用户管理接口（继承自Server-10）:\n";
    std::cout << "  POST /api/users/register      - JSON格式注册\n";
    std::cout << "  POST /api/users/login         - JSON格式登录\n";
    std::cout << "  GET  /api/users?id=xxx        - 获取用户信息\n";
    std::cout << "  POST /api/echo                - JSON回显测试\n";
    std::cout << "\n🌐 传统接口（兼容Server-9）:\n";
    std::cout << "  POST /register                - Form格式注册\n";
    std::cout << "  POST /login                   - Form格式登录\n";
    std::cout << "======================================\n";
    std::cout << "✅ Server-12 is ready!\n";
    std::cout << "🎯 Now supports multi-turn conversations!\n";
    std::cout << "======================================\n\n";

    Database db("users.db"); // 初始化数据库
    HttpServer server(port, 10, db);

    // ★ Server-12更新：传递inference和sessionMgr对象到RESTful路由
    server.setupRESTfulRoutes(inference, sessionMgr);

    // 兼容旧的路由
    server.setupRoutes();

    server.start();
    return 0;
}
