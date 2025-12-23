#include "HttpServer.h"
#include "Database.h"
#include "inference/ModelManager.h"
#include <iostream>
#include <cstdlib>

int main() {
    // 1. 初始化数据库
    Database db("users.db");

    // 2. 加载量化模型（从环境变量或使用默认路径）
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/tinyllama-q4.gguf";
    if (!ModelManager::instance().loadModel(modelPath,
                                            /*n_ctx=*/2048,
                                            /*n_threads=*/4)) {
        std::cerr << "Model load failed\n";
        return 1;
    }
    // 3. 创建 HTTPServer
    HttpServer server(8080, /*max_events=*/10, db);

    // 4. 注册路由
    server.setupRoutes();      // GET /, /register, /login
    server.setupInferRoute();  // POST /infer

    // 5. 启动服务器
    server.start();
    return 0;
}
