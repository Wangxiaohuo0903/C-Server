#include "HttpServer.h"
#include "Database.h"
#include "inference/ModelManager.h"
#include <iostream>
#include <fstream>
#include <sstream>

// 简易JSON解析：提取所有templates中的prompt字段
std::vector<std::string> loadTemplatesFromJSON(const std::string& filename) {
    std::vector<std::string> prompts;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "⚠️  无法打开模板文件: " << filename << "\n";
        std::cerr << "   将使用默认模板\n";
        return prompts;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // 简易解析："prompt": "..."
    size_t pos = 0;
    while ((pos = content.find("\"prompt\":", pos)) != std::string::npos) {
        pos += 9; // skip "prompt":

        // 跳过空白字符
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\n')) {
            pos++;
        }

        if (pos >= content.size() || content[pos] != '"') continue;
        pos++; // skip opening "

        size_t end = pos;
        std::string prompt;

        // 查找结束的双引号（处理转义）
        while (end < content.size()) {
            if (content[end] == '\\' && end + 1 < content.size()) {
                // 处理转义字符
                char next = content[end + 1];
                if (next == 'n') {
                    prompt += '\n';
                } else if (next == 't') {
                    prompt += '\t';
                } else if (next == '"') {
                    prompt += '"';
                } else if (next == '\\') {
                    prompt += '\\';
                } else {
                    prompt += next;
                }
                end += 2;
            } else if (content[end] == '"') {
                break; // 找到结束引号
            } else {
                prompt += content[end];
                end++;
            }
        }

        if (!prompt.empty()) {
            prompts.push_back(prompt);
        }

        pos = end + 1;
    }

    return prompts;
}

int main() {
    // 1. 初始化数据库
    Database db("users.db");

    // 2. 加载量化模型（示例路径，可根据实际路径修改）
   const std::string modelPath = "/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/tinyllama-q4.gguf";
    if (!ModelManager::instance().loadModel(modelPath,
                                            /*n_ctx=*/2048,
                                            /*n_threads=*/4)) {
        std::cerr << "Model load failed\n";
        return 1;
    }

    // 2.5 预热常用模板（提升缓存命中率）
    // 优先从配置文件加载代码修改专用模板
    std::cout << "\n📚 加载代码修改专用模板...\n";
    std::vector<std::string> warmup_prompts = loadTemplatesFromJSON("warmup_templates_code.json");

    // 如果配置文件加载失败，使用默认模板
    if (warmup_prompts.empty()) {
        std::cout << "📝 使用默认通用模板\n";
        warmup_prompts = {
            "<|system|>\nYou are a helpful coding assistant.",
            "<|system|>\n你是一个编程助手。",
            "<|system|>\nYou are a Python expert.",
            "<|system|>\n你是一个友好的AI助手。",
            "<|system|>\nYou are a helpful assistant."
        };
    } else {
        std::cout << "✅ 成功加载 " << warmup_prompts.size() << " 个代码修改专用模板\n";
    }

    ModelManager::instance().warmupCache(warmup_prompts);

    // 3. 创建 HTTPServer
    HttpServer server(8080, /*max_events=*/10, db);

    // 4. 注册路由
    server.setupRoutes();      // GET /, /register, /login
    server.setupInferRoute();  // POST /infer

    // 5. 启动服务器
    server.start();
    return 0;
}
