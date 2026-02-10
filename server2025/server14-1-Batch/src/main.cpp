/*==========================================================
 * Server-13: KV Cache 优化的多轮对话AI服务器
 *
 * 【核心升级】KV Cache 持久化
 *
 * 新增功能（基于 Server-12）：
 * - ✅ SessionManager：多会话管理
 * - ✅ 类似ChatGPT的多对话窗口
 * - ✅ 支持会话历史记录
 * - 🚀 KV Cache 持久化：大幅提升性能（5-10倍）
 * - 🚀 增量推理：只处理新 token，复用历史计算
 * - 🚀 内存优化：智能管理 context 生命周期
 *
 * 性能对比：
 * - Server-12: 每次对话处理全部历史 token
 * - Server-13: 每次对话只处理新增 token（快5-10倍！）
 *
 * 适用场景：
 * - 长时间多轮对话
 * - 高并发用户聊天
 * - 上下文敏感的对话系统
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SessionManager.h"  // 会话管理
#include "inference/ModelManager.h"  // KV Cache 优化的推理引擎
#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "=== Server-14-1: Batch Processing AI Server ===\n";
    std::cout << "🚀 Feature: Continuous Batching & KV Cache Reuse\n\n";

    // 1. 初始化数据库
    Database db("users.db");
    std::cout << "✓ Database initialized\n";

    // 2. 加载量化模型（从环境变量或使用默认路径）
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/smollm-360m-q4.gguf";
    
    // Batch Size 配置
    int batch_size = 8;
    if (const char* envBatch = std::getenv("BATCH_SIZE")) {
        batch_size = std::atoi(envBatch);
    }

    std::cout << "Loading model: " << modelPath << " (Batch Size: " << batch_size << ")\n";
    if (!ModelManager::instance().loadModel(modelPath,
                                            /*n_ctx=*/2048,
                                            /*n_threads=*/4,
                                            batch_size)) {
        std::cerr << "❌ Model load failed\n";
        return 1;
    }
    std::cout << "✓ Model loaded successfully\n";

    // 3. Server-12新增：创建SessionManager（全局单例）
    static SessionManager sessionManager;
    std::cout << "✓ SessionManager initialized\n";

    // 4. 创建HttpServer
    HttpServer server(8080, /*max_events=*/10, db);

    // 5. 注册路由（在start()中完成，但需要传入sessionManager）
    // 为了简化，我们需要修改HttpServer::start()来接受sessionManager
    // 这里我们通过修改HttpServer类来支持

    std::cout << "\n🚀 Server-12 starting on port 8080...\n";
    std::cout << "   Visit http://localhost:8080/multichat.html\n";
    std::cout << "   Press Ctrl+C to stop\n\n";

    // 启动服务器（传入sessionManager）
    server.start(sessionManager);

    return 0;
}
