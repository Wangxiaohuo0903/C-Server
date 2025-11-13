// K值优化测试程序 - 对比K=3/5/7的性能
#include "inference/ModelManager.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <thread>

struct TestResult {
    int K;
    int latency_ms;
    float acceptance_rate;
    float speedup;
    int avg_draft_time_ms;
    int avg_verify_time_ms;
};

TestResult runTest(int K_value, const std::string& test_prompt, int max_tokens, float temperature) {
    std::cout << "\n======================================== \n";
    std::cout << "   测试 K=" << K_value << "\n";
    std::cout << "======================================== \n";

    // 1. 配置推测式解码
    SpeculativeConfig config = SpeculativeConfig::createForAppleSilicon();
    config.draft_tokens_K = K_value;
    config.enable_dynamic_K = false;  // 禁用动态K，保证测试准确性

    std::cout << "📦 加载主模型 (DeepSeek-Coder-6.7B)...\n";
    const std::string verifier_path = "/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/deepseek-coder-6.7b-instruct-q4.gguf";

    if (!ModelManager::instance().loadModel(verifier_path, /*n_ctx=*/2048, /*n_threads=*/4)) {
        std::cerr << "❌ 主模型加载失败\n";
        return {K_value, -1, 0, 0, 0, 0};
    }
    std::cout << "✅ 主模型加载成功\n";

    // 2. 启用推测式解码
    std::cout << "🚀 启用推测式解码 (K=" << K_value << ")...\n";
    const std::string drafter_path = "/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/deepseek-coder-1.3b-instruct-q4.gguf";

    if (!ModelManager::instance().enableSpeculativeDecoding(drafter_path, &config)) {
        std::cerr << "❌ 推测式解码启用失败\n";
        return {K_value, -1, 0, 0, 0, 0};
    }
    std::cout << "✅ 推测式解码已启用\n";

    // 3. 运行推理
    std::cout << "🔥 运行推理...\n";
    auto start = std::chrono::high_resolution_clock::now();
    std::string result = ModelManager::instance().infer(
        "test_k_" + std::to_string(K_value),
        test_prompt,
        max_tokens,
        temperature
    );
    auto end = std::chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // 4. 提取统计信息
    std::string stats = ModelManager::instance().getSpeculativeStats();

    // 解析统计信息 (简单实现 - 实际应该更robust)
    float acceptance_rate = 0.0f;
    float speedup = 0.0f;
    int avg_draft_ms = 0;
    int avg_verify_ms = 0;

    size_t pos = stats.find("Acceptance rate:");
    if (pos != std::string::npos) {
        sscanf(stats.c_str() + pos, "Acceptance rate: %f%%", &acceptance_rate);
    }

    pos = stats.find("Speedup:");
    if (pos != std::string::npos) {
        sscanf(stats.c_str() + pos, "Speedup: %fx", &speedup);
    }

    pos = stats.find("Avg draft time:");
    if (pos != std::string::npos) {
        sscanf(stats.c_str() + pos, "Avg draft time: %d", &avg_draft_ms);
    }

    pos = stats.find("Avg verify time:");
    if (pos != std::string::npos) {
        sscanf(stats.c_str() + pos, "Avg verify time: %d", &avg_verify_ms);
    }

    std::cout << "\n📊 结果:\n";
    std::cout << "  延迟: " << latency << " ms\n";
    std::cout << "  接受率: " << acceptance_rate << "%\n";
    std::cout << "  理论加速: " << speedup << "x\n";
    std::cout << "  Draft时间: " << avg_draft_ms << " ms\n";
    std::cout << "  Verify时间: " << avg_verify_ms << " ms\n";

    return {K_value, (int)latency, acceptance_rate, speedup, avg_draft_ms, avg_verify_ms};
}

int main() {
    std::cout << "========================================\n";
    std::cout << "   K值优化测试\n";
    std::cout << "   目标: 找到最佳K值以最大化实际加速\n";
    std::cout << "========================================\n";

    const std::string test_prompt = "Write a Python function to calculate fibonacci numbers";
    const int max_tokens = 100;
    const float temperature = 0.1;

    std::vector<int> K_values = {3, 5, 7};
    std::vector<TestResult> results;

    // 依次测试每个K值
    for (int K : K_values) {
        TestResult result = runTest(K, test_prompt, max_tokens, temperature);
        if (result.latency_ms > 0) {
            results.push_back(result);
        }

        // 禁用推测式解码，为下一个测试做准备
        ModelManager::instance().disableSpeculativeDecoding();

        std::cout << "\n⏸️  等待3秒让系统稳定...\n";
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    // 对比结果
    std::cout << "\n\n========================================\n";
    std::cout << "   测试结果对比\n";
    std::cout << "========================================\n\n";

    std::cout << std::left << std::setw(6) << "K值"
              << std::setw(12) << "延迟(ms)"
              << std::setw(15) << "接受率(%)"
              << std::setw(12) << "理论加速"
              << std::setw(15) << "Draft(ms)"
              << std::setw(15) << "Verify(ms)"
              << "tokens/ms\n";
    std::cout << "--------------------------------------------------------------------------------\n";

    int best_K = -1;
    int best_latency = INT_MAX;

    for (const auto& r : results) {
        float tokens_per_ms = max_tokens / (float)r.latency_ms;

        std::cout << std::left << std::setw(6) << r.K
                  << std::setw(12) << r.latency_ms
                  << std::setw(15) << std::fixed << std::setprecision(2) << r.acceptance_rate
                  << std::setw(12) << r.speedup
                  << std::setw(15) << r.avg_draft_time_ms
                  << std::setw(15) << r.avg_verify_time_ms
                  << std::setprecision(3) << tokens_per_ms
                  << "\n";

        if (r.latency_ms < best_latency) {
            best_latency = r.latency_ms;
            best_K = r.K;
        }
    }

    std::cout << "\n🏆 最优配置: K=" << best_K << " (延迟 " << best_latency << " ms)\n";

    // 分析
    std::cout << "\n\n📈 性能分析\n";
    std::cout << "========================================\n";

    if (results.size() >= 2) {
        // K=3 vs K=5
        auto& r3 = results[0];
        auto& r5 = results[1];

        std::cout << "\nK=3 vs K=5:\n";
        std::cout << "  接受率变化: " << (r5.acceptance_rate - r3.acceptance_rate) << "%\n";
        std::cout << "  延迟变化: " << (r5.latency_ms - r3.latency_ms) << " ms\n";
        std::cout << "  Draft时间变化: " << (r5.avg_draft_time_ms - r3.avg_draft_time_ms) << " ms\n";

        if (results.size() >= 3) {
            // K=5 vs K=7
            auto& r7 = results[2];

            std::cout << "\nK=5 vs K=7:\n";
            std::cout << "  接受率变化: " << (r7.acceptance_rate - r5.acceptance_rate) << "%\n";
            std::cout << "  延迟变化: " << (r7.latency_ms - r5.latency_ms) << " ms\n";
            std::cout << "  Draft时间变化: " << (r7.avg_draft_time_ms - r5.avg_draft_time_ms) << " ms\n";
        }
    }

    std::cout << "\n✅ 测试完成!\n";
    return 0;
}
