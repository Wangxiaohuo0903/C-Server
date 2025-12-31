/*==========================================================
 * Server-14: Authenticated AI Server with Persistent Chat
 *
 * Upgrades from Server-13:
 * - JWT authentication for API endpoints
 * - Real-time database persistence for chat messages
 * - User-scoped session isolation
 * - Login/registration UI
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SessionManager.h"
#include "ModelManagerV2.h"
#include "JWTAuth.h"  // Server-14: JWT authentication
#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "=== Server-14: Authenticated AI Server ===\n";

    // Server-14: Initialize JWT secret key
    JWTAuth::setSecretKey("server14-secret-key-change-in-production-abc123");
    std::cout << "✓ JWT authentication initialized\n";

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

    // 3. 初始化 SessionManager (Server-14: 传入 Database 引用)
    static SessionManager sessionManager(db);
    std::cout << "✓ SessionManager initialized with database persistence\n";

    // 4. 创建HttpServer
    HttpServer server(8080, /*max_events=*/10, db);

    std::cout << "\n🚀 Server-14 starting on port 8080...\n";
    std::cout << "   Host port mapping: http://localhost:6060\n";
    std::cout << "   Features: JWT Auth, DB Persistence, User Isolation\n";
    std::cout << "   Press Ctrl+C to stop\n\n";

    // 启动服务器
    // The routing logic inside HttpServer needs to use ModelManagerV2
    server.start(sessionManager);

    return 0;
}