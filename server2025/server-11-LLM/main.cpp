/*==========================================================
 * Server-11: llama.cpp集成 + 单轮推理
 *
 * 新增功能：
 * 1. SimpleInference类 - 封装llama.cpp推理逻辑
 * 2. GGUF格式模型加载
 * 3. 单轮文本生成（无历史记录）
 * 4. /infer-simple API接口
 *
 * 继承自Server-10：
 * - JSON解析和RESTful API
 * - 查询参数支持
 * - 用户注册/登录功能
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SimpleInference.h"
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    int port = 8080; // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }

    std::cout << "=== Server-11: llama.cpp集成 + 单轮推理 ===\n";
    std::cout << "Port: " << port << "\n\n";

    // ★ Server-11新增：初始化推理引擎
    std::cout << "[1/3] Initializing inference engine...\n";
    SimpleInference inference;

    // 从环境变量或使用默认路径加载模型
    const char* model_path_env = std::getenv("MODEL_PATH");
    std::string model_path = model_path_env ? model_path_env : "../models/tinyllama-q4.gguf";

    std::cout << "[2/3] Loading model: " << model_path << "\n";
    if (!inference.loadModel(model_path)) {
        std::cerr << "\n❌ Failed to load model!\n";
        std::cerr << "Please set MODEL_PATH environment variable or place model at: " << model_path << "\n";
        std::cerr << "Example: export MODEL_PATH=/path/to/your/model.gguf\n\n";
        return 1;
    }

    std::cout << "[3/3] Starting HTTP server...\n\n";

    std::cout << "📡 AI推理接口（★ Server-11新增）:\n";
    std::cout << "  POST /infer-simple       - 单轮推理（JSON格式）\n";
    std::cout << "                             Request:  {\"prompt\":\"...\", \"max_tokens\":\"64\"}\n";
    std::cout << "                             Response: {\"success\":true, \"response\":\"...\"}\n";
    std::cout << "\n📋 用户管理接口（继承自Server-10）:\n";
    std::cout << "  POST /api/users/register - JSON格式注册\n";
    std::cout << "  POST /api/users/login    - JSON格式登录\n";
    std::cout << "  GET  /api/users?id=xxx   - 获取用户信息\n";
    std::cout << "  POST /api/echo           - JSON回显测试\n";
    std::cout << "\n🌐 传统接口（兼容Server-9）:\n";
    std::cout << "  POST /register           - Form格式注册\n";
    std::cout << "  POST /login              - Form格式登录\n";
    std::cout << "======================================\n";
    std::cout << "✅ Server-11 is ready!\n";
    std::cout << "======================================\n\n";

    Database db("users.db"); // 初始化数据库
    HttpServer server(port, 10, db);

    // ★ Server-11更新：传递inference对象到RESTful路由
    server.setupRESTfulRoutes(inference);

    // 兼容旧的路由
    server.setupRoutes();

    server.start();
    return 0;
}
