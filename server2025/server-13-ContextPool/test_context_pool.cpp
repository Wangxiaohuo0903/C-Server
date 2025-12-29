/**
 * Context Pool 功能测试程序
 *
 * 用途：演示 Server-13 的核心优化功能
 * - SessionContextPool（KV 缓存复用）
 * - BatchInferenceEngine（批处理推理）
 * - ModelManagerV2（统一管理）
 */

#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include "ModelManagerV2.h"

// 辅助函数：计时器
class Timer {
    std::chrono::steady_clock::time_point start_;
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}

    double elapsed_ms() {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

    void reset() {
        start_ = std::chrono::steady_clock::now();
    }
};

// 测试1：KV 缓存复用效果
void test_kv_cache_reuse() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 1: KV Cache Reuse" << std::endl;
    std::cout << "========================================\n" << std::endl;

    const std::string session_id = "test_session_001";

    // 第一轮对话
    std::cout << "[Round 1] User: 你好" << std::endl;
    Timer t1;
    std::string reply1 = ModelManagerV2::instance().inferWithCache(
        session_id, "User: 你好\nAssistant:", 30, 0.7
    );
    double time1 = t1.elapsed_ms();
    std::cout << "AI: " << reply1 << std::endl;
    std::cout << "Time: " << time1 << "ms\n" << std::endl;

    // 第二轮对话（应该复用 KV 缓存）
    std::cout << "[Round 2] User: 介绍一下Docker" << std::endl;
    std::string full_prompt2 = "User: 你好\nAssistant: " + reply1 +
                                "\nUser: 介绍一下Docker\nAssistant:";
    Timer t2;
    std::string reply2 = ModelManagerV2::instance().inferWithCache(
        session_id, full_prompt2, 50, 0.7
    );
    double time2 = t2.elapsed_ms();
    std::cout << "AI: " << reply2 << std::endl;
    std::cout << "Time: " << time2 << "ms" << std::endl;
    std::cout << "Speed-up: " << (time1 / time2) << "x\n" << std::endl;

    // 第三轮对话
    std::cout << "[Round 3] User: 它和虚拟机有什么区别？" << std::endl;
    std::string full_prompt3 = full_prompt2 + reply2 +
                                "\nUser: 它和虚拟机有什么区别？\nAssistant:";
    Timer t3;
    std::string reply3 = ModelManagerV2::instance().inferWithCache(
        session_id, full_prompt3, 50, 0.7
    );
    double time3 = t3.elapsed_ms();
    std::cout << "AI: " << reply3 << std::endl;
    std::cout << "Time: " << time3 << "ms" << std::endl;
    std::cout << "Speed-up: " << (time1 / time3) << "x\n" << std::endl;

    // 显示统计信息
    auto stats = ModelManagerV2::instance().getStats();
    std::cout << "Session Pool Stats:" << std::endl;
    std::cout << "  Total sessions: " << stats.session_pool_stats.total_sessions << std::endl;
    std::cout << "  Cache hits: " << stats.session_pool_stats.total_hits << std::endl;
    std::cout << "  Cache misses: " << stats.session_pool_stats.total_misses << std::endl;
}

// 测试2：批处理推理效果
void test_batch_inference() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 2: Batch Inference" << std::endl;
    std::cout << "========================================\n" << std::endl;

    const int num_requests = 5;
    std::vector<std::future<std::string>> futures;

    std::cout << "Submitting " << num_requests << " requests..." << std::endl;
    Timer t;

    // 提交多个请求
    for (int i = 0; i < num_requests; ++i) {
        std::string session_id = "batch_test_" + std::to_string(i);
        std::string prompt = "User: What is " + std::to_string(i) + " + 1?\nAssistant:";

        futures.push_back(
            ModelManagerV2::instance().inferBatch(session_id, prompt, 20, 0.7)
        );
    }

    std::cout << "Waiting for results..." << std::endl;

    // 获取结果
    for (int i = 0; i < num_requests; ++i) {
        std::string result = futures[i].get();
        std::cout << "Request " << i << ": " << result << std::endl;
    }

    double total_time = t.elapsed_ms();
    std::cout << "\nTotal time: " << total_time << "ms" << std::endl;
    std::cout << "Avg per request: " << (total_time / num_requests) << "ms" << std::endl;

    // 显示批处理统计
    auto stats = ModelManagerV2::instance().getStats();
    if (stats.batch_enabled) {
        std::cout << "\nBatch Engine Stats:" << std::endl;
        std::cout << "  Total requests: " << stats.batch_stats.total_requests << std::endl;
        std::cout << "  Total batches: " << stats.batch_stats.total_batches << std::endl;
        std::cout << "  Avg batch size: " << stats.batch_stats.avg_batch_size << std::endl;
        std::cout << "  Avg batch time: " << stats.batch_stats.avg_batch_time_ms << "ms" << std::endl;
    }
}

// 测试3：多会话并发
void test_multi_session() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Test 3: Multi-Session Concurrent" << std::endl;
    std::cout << "========================================\n" << std::endl;

    const int num_sessions = 10;
    std::vector<std::thread> threads;
    Timer t;

    for (int i = 0; i < num_sessions; ++i) {
        threads.emplace_back([i]() {
            std::string session_id = "session_" + std::to_string(i);
            std::string prompt = "User: Count from 1 to " + std::to_string(i + 1) + "\nAssistant:";

            std::string result = ModelManagerV2::instance().inferWithCache(
                session_id, prompt, 20, 0.7
            );

            std::cout << "Session " << i << " completed: " << result.substr(0, 30) << "..." << std::endl;
        });
    }

    // 等待所有线程完成
    for (auto& th : threads) {
        th.join();
    }

    double total_time = t.elapsed_ms();
    std::cout << "\nTotal time: " << total_time << "ms" << std::endl;
    std::cout << "Avg per session: " << (total_time / num_sessions) << "ms" << std::endl;

    // 显示统计
    auto stats = ModelManagerV2::instance().getStats();
    std::cout << "\nSession Pool Stats:" << std::endl;
    std::cout << "  Total sessions: " << stats.session_pool_stats.total_sessions << std::endl;
    std::cout << "  Cache hit rate: "
              << (100.0 * stats.session_pool_stats.total_hits /
                  (stats.session_pool_stats.total_hits + stats.session_pool_stats.total_misses))
              << "%" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "=====================================" << std::endl;
    std::cout << "Server-13 Context Pool Test" << std::endl;
    std::cout << "=====================================\n" << std::endl;

    // 获取模型路径
    const char* model_path = std::getenv("MODEL_PATH");
    if (!model_path) {
        model_path = "../models/smollm-360m-q4_k_m.gguf";  // 默认路径
    }

    std::cout << "Loading model: " << model_path << std::endl;

    // 配置参数
    const int n_ctx = 2048;
    const int n_threads = 4;
    const int max_sessions = 20;
    const bool enable_batch = true;  // 启用批处理
    const int batch_size = 4;

    // 加载模型
    bool success = ModelManagerV2::instance().loadModel(
        model_path,
        n_ctx,
        n_threads,
        max_sessions,
        enable_batch,
        batch_size
    );

    if (!success) {
        std::cerr << "Failed to load model!" << std::endl;
        return 1;
    }

    std::cout << "Model loaded successfully!\n" << std::endl;

    // 运行测试
    try {
        // 测试1：KV 缓存复用
        test_kv_cache_reuse();

        // 测试2：批处理推理（如果启用）
        if (enable_batch) {
            test_batch_inference();
        }

        // 测试3：多会话并发
        test_multi_session();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=====================================" << std::endl;
    std::cout << "All tests completed successfully!" << std::endl;
    std::cout << "=====================================" << std::endl;

    return 0;
}
