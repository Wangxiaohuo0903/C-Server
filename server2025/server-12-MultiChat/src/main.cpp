/*==========================================================
 * Server-12: 多轮对话AI服务器
 *
 * 新增功能：
 * - SessionManager：多会话管理
 * - 类似ChatGPT的多对话窗口
 * - 支持会话历史记录
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SessionManager.h"  // Server-12新增
#include "inference/ModelManager.h"
#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "=== Server-12: Multi-Chat AI Server ===\n";

    // 1. 初始化数据库
    Database db("users.db");
    std::cout << "✓ Database initialized\n";

    // 2. 加载量化模型（从环境变量或使用默认路径）
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/smollm-360m-q4.gguf";

    std::cout << "Loading model: " << modelPath << "\n";
    if (!ModelManager::instance().loadModel(modelPath,
                                            /*n_ctx=*/2048,
                                            /*n_threads=*/4)) {
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

    // 启动服务器（需要传入sessionManager）
    // server.start(sessionManager);
    server.start();  // 临时使用原版start()

    return 0;
}
