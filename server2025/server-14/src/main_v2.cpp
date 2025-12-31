/*==========================================================
 * Server-14: 高性能AI推理服务器（JWT认证 + 聊天历史持久化）
 *
 * 核心优化：
 * - SessionContextPool：KV 缓存复用（4-10倍性能提升）
 * - BatchInferenceEngine：批处理推理（5倍吞吐量提升）
 * - ModelManagerV2：统一管理
 * - JWTAuth：用户认证
 * - Database：聊天历史持久化
 *=========================================================*/

#include "HttpServer.h"
#include "Database.h"
#include "SessionManager.h"
#include "ModelManagerV2.h"
#include "JWTAuth.h"
#include <iostream>
#include <cstdlib>

int main() {
    std::cout << "=== Server-14: JWT Auth + Chat History Persistence ===\n";
    std::cout << "=== 高性能AI推理服务器（带用户认证）===\n\n";

    // 1. 初始化 JWT 密钥
    JWTAuth::setSecretKey("server14-secret-key-change-in-production-abc123");
    std::cout << "✓ JWT Auth initialized\n";

    // 2. 初始化数据库
    Database db("users.db");
    std::cout << "✓ Database initialized\n";

    // 2. 加载量化模型（从环境变量或使用默认路径）
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/smollm-360m-q4.gguf";

    std::cout << "Loading model: " << modelPath << "\n";

    // Server-14: DeepSeek-R1 optimized configuration
    // 配置参数
    const int n_ctx = 16384;          // 上下文长度（DeepSeek-R1 需要大窗口用于思考链）
    const int n_threads = 4;          // CPU 线程数
    const int max_sessions = 20;      // 最大会话数（减少以节省内存，因为 n_ctx 很大）
    const bool enable_batch = false;  // 批处理（可选，高并发时启用）
    const int batch_size = 8;         // 批次大小

    if (!ModelManagerV2::instance().loadModel(
            modelPath,
            n_ctx,
            n_threads,
            max_sessions,
            enable_batch,
            batch_size)) {
        std::cerr << "❌ Model load failed\n";
        return 1;
    }
    std::cout << "✓ Model loaded successfully (ModelManagerV2)\n";
    std::cout << "  - n_ctx: " << n_ctx << "\n";
    std::cout << "  - n_threads: " << n_threads << "\n";
    std::cout << "  - max_sessions: " << max_sessions << " (KV Cache Pool)\n";
    std::cout << "  - batch_inference: " << (enable_batch ? "enabled" : "disabled") << "\n";

    // 3. 创建 SessionManager（用于会话管理）
    static SessionManager sessionManager(db);
    std::cout << "✓ SessionManager initialized\n";

    // 4. 创建 HttpServer
    HttpServer server(8080, /*max_events=*/10, db);

    std::cout << "\n🚀 Server-14 starting on port 8080...\n";
    std::cout << "   📊 Features:\n";
    std::cout << "      - JWT Token Authentication\n";
    std::cout << "      - Chat History Persistence (SQLite)\n";
    std::cout << "      - KV Cache Reuse (SessionContextPool)\n";
    std::cout << "      - Multi-session Support\n";
    std::cout << "      - Performance: 4-10x faster for multi-turn dialogues\n";
    std::cout << "\n   🌐 Access:\n";
    std::cout << "      - Login: http://localhost:8080/login.html\n";
    std::cout << "      - Web UI: http://localhost:8080/multichat.html\n";
    std::cout << "      - API: http://localhost:8080/api/sessions\n";
    std::cout << "\n   Press Ctrl+C to stop\n\n";

    // 启动服务器
    server.start(sessionManager);

    // 打印统计信息（如果服务器停止）
    auto stats = ModelManagerV2::instance().getStats();
    std::cout << "\n=== Server Statistics ===\n";
    std::cout << "Sessions: " << stats.session_pool_stats.total_sessions << "\n";
    std::cout << "Cache hits: " << stats.session_pool_stats.total_hits << "\n";
    std::cout << "Cache misses: " << stats.session_pool_stats.total_misses << "\n";

    return 0;
}
