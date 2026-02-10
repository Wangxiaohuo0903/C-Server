/*==========================================================
 * Server-11: llama.cpp集成 + 单轮推理
 *
 * ★ 新增功能（相比Server-9）：
 * 1. SimpleInference类 - 封装llama.cpp推理逻辑
 *    - GGUF模型加载
 *    - 单轮文本生成
 *    - KV-cache优化
 *
 * 2. GGUF格式模型加载
 *    - 支持量化模型（Q4_K_M等）
 *    - 环境变量配置（MODEL_PATH）
 *
 * 3. 单轮文本生成（无历史记录）
 *    - 输入：prompt + max_tokens
 *    - 输出：LLM生成的文本
 *
 * 4. /infer-simple API接口
 *    - RESTful风格
 *    - JSON格式
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

int main(int argc, char *argv[])
{
    // 步骤1: 解析命令行参数（端口号）
    int port = 8080; // 默认端口
    if (argc > 1)
    {
        port = std::stoi(argv[1]); // 从命令行获取端口
    }

    std::cout << "=== Server-11: llama.cpp集成 + 单轮推理 ===\n";
    std::cout << "Port: " << port << "\n\n";

    /* ========================================
     * 步骤2: 初始化LLM推理引擎 ★ Server-11新增
     * ======================================== */
    std::cout << "[1/3] Initializing inference engine...\n";
    SimpleInference inference; // 创建推理对象

    /* ========================================
     * 步骤3: 获取模型路径 ★ Server-11新增
     * ======================================== */
    // 优先使用环境变量MODEL_PATH，否则使用默认路径
    // 这样设计的好处：
    // 1. Docker部署时可以通过-e MODEL_PATH=...灵活指定模型
    // 2. 无需修改代码即可切换模型
    const char *model_path_env = std::getenv("MODEL_PATH");
    std::string model_path = model_path_env ? model_path_env : "../models/tinyllama-q4.gguf";

    /* ========================================
     * 步骤4: 加载GGUF模型 ★ Server-11新增
     * ======================================== */
    std::cout << "[2/3] Loading model: " << model_path << "\n";
    if (!inference.loadModel(model_path))
    {
        std::cerr << "\n❌ Failed to load model!\n";
        std::cerr << "Please set MODEL_PATH environment variable or place model at: " << model_path << "\n";
        std::cerr << "Example: export MODEL_PATH=/path/to/your/model.gguf\n\n";
        return 1; // 加载失败，退出程序
    }

    std::cout << "[3/3] Starting HTTP server...\n\n";

    /* ========================================
     * 步骤5: 初始化数据库和HTTP服务器
     * ======================================== */
    Database db("users.db");         // SQLite数据库（用户管理）
    HttpServer server(port, 10, db); // HTTP服务器（端口、线程池大小、数据库）

    /* ========================================
     * 步骤6: 设置RESTful路由 ★ Server-11更新
     * ======================================== */
    // ★ 关键变化：传递inference对象
    // Server-10: server.setupRESTfulRoutes()
    // Server-11: server.setupRESTfulRoutes(inference)
    //
    // 这样Router中的/infer-simple端点就可以通过lambda捕获inference引用
    // 调用inference.generate()进行LLM推理
    server.setupRESTfulRoutes(inference);

    /* ========================================
     * 步骤7: 设置传统路由（兼容Server-9）
     * ======================================== */
    server.setupRoutes(); // HTML表单接口

    /* ========================================
     * 步骤8: 启动服务器（阻塞调用）
     * ======================================== */
    server.start(); // 进入主事件循环，处理HTTP请求
    return 0;
}
