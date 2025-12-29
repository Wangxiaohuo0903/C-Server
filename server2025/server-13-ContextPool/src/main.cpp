/*==========================================================
 * Server-13: Context Pool & Batching AI Server
 *
 * Major Upgrades:
 * - ModelManagerV2: Manages context pool and batching.
 * - SessionContextPool: Reuses KV cache between requests.
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SessionManager.h" 
#include "ModelManagerV2.h" // Use the new V2 Model Manager
#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "=== Server-13: Context Pool & Batching AI Server ===\n";

    // 1. 初始化数据库 (for UI session management)
    Database db("users.db");
    std::cout << "✓ Database initialized\n";

    // 2. 加载模型 using ModelManagerV2
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf";

    const int n_ctx = 2048;
    const int n_threads = std::getenv("OMP_NUM_THREADS") ? std::atoi(std::getenv("OMP_NUM_THREADS")) : 4;
    const int max_sessions = 100;
    const bool enable_batch = false; // 禁用批处理以进行简单测试
    const int batch_size = 8;

    std::cout << "Loading model: " << modelPath << "\n";
    if (!ModelManagerV2::instance().loadModel(modelPath,
                                            n_ctx,
                                            n_threads,
                                            max_sessions,
                                            enable_batch,
                                            batch_size)) {
        std::cerr << "❌ Model load failed (V2)\n";
        return 1;
    }
    std::cout << "✓ Model loaded successfully (V2)\n";

    // 3. 初始化 UI SessionManager (for chat history list)
    static SessionManager sessionManager;
    std::cout << "✓ UI SessionManager initialized\n";

    // 4. 创建HttpServer
    HttpServer server(8080, /*max_events=*/10, db);

    std::cout << "\n🚀 Server-13 starting on port 8080...\n";
    std::cout << "   Host port mapping: http://localhost:6060\n";
    std::cout << "   Press Ctrl+C to stop\n\n";

    // 启动服务器
    // The routing logic inside HttpServer needs to use ModelManagerV2
    server.start(sessionManager);

    return 0;
}