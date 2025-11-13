// 推测式解码功能测试程序
#include "inference/ModelManager.h"
#include <iostream>
#include <chrono>
#include <iomanip>

int main() {
    std::cout << "========================================\n";
    std::cout << "   推测式解码功能测试\n";
    std::cout << "========================================\n\n";

    // 1. 加载主模型 (Verifier - DeepSeek-Coder-6.7B)
    std::cout << "📦 步骤1: 加载主模型 (DeepSeek-Coder-6.7B Verifier)...\n";
    const std::string verifier_path = "/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/deepseek-coder-6.7b-instruct-q4.gguf";

    if (!ModelManager::instance().loadModel(verifier_path, /*n_ctx=*/2048, /*n_threads=*/4)) {
        std::cerr << "❌ 主模型加载失败\n";
        return 1;
    }
    std::cout << "✅ 主模型加载成功\n\n";

    // 2. 启用推测式解码
    std::cout << "🚀 步骤2: 启用推测式解码 (DeepSeek-Coder-1.3B Drafter)...\n";
    const std::string drafter_path = "/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/deepseek-coder-1.3b-instruct-q4.gguf";

    if (!ModelManager::instance().enableSpeculativeDecoding(drafter_path)) {
        std::cerr << "❌ 推测式解码启用失败\n";
        return 1;
    }
    std::cout << "✅ 推测式解码已启用\n\n";

    // 3. 运行兼容性检查
    std::cout << "🔍 步骤3: 兼容性检查...\n";
    std::string compat_report = ModelManager::instance().checkSpeculativeCompatibility();
    std::cout << compat_report << "\n";

    // 4. 测试推理（贪婪解码 vs 推测式解码）
    std::cout << "\n📊 步骤4: 性能对比测试\n";
    std::cout << "----------------------------------------\n";

    const std::string test_prompt = "Write a Python function to calculate fibonacci numbers";
    const int max_tokens = 100;
    const float temperature = 0.1;  // 低温度，利于推测式解码

    // 测试1: 贪婪解码 (baseline)
    std::cout << "\n[测试1] 贪婪解码 (baseline):\n";
    auto start1 = std::chrono::high_resolution_clock::now();
    std::string result_greedy = ModelManager::instance().infer(
        "test_greedy", test_prompt, max_tokens, temperature
    );
    auto end1 = std::chrono::high_resolution_clock::now();
    auto duration1 = std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count();

    std::cout << "⏱️  延迟: " << duration1 << " ms\n";
    std::cout << "📝 输出长度: " << result_greedy.size() << " 字符\n";
    std::cout << "🔤 输出预览: " << result_greedy.substr(0, 150) << "...\n";

    // 测试2: 推测式解码
    std::cout << "\n[测试2] 推测式解码:\n";
    auto start2 = std::chrono::high_resolution_clock::now();
    std::string result_speculative = ModelManager::instance().infer(
        "test_spec", test_prompt, max_tokens, temperature
    );
    auto end2 = std::chrono::high_resolution_clock::now();
    auto duration2 = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count();

    std::cout << "⏱️  延迟: " << duration2 << " ms\n";
    std::cout << "📝 输出长度: " << result_speculative.size() << " 字符\n";
    std::cout << "🔤 输出预览: " << result_speculative.substr(0, 150) << "...\n";

    // 5. 显示统计信息
    std::cout << "\n📈 步骤5: 推测式解码统计信息\n";
    std::cout << "----------------------------------------\n";
    std::string stats = ModelManager::instance().getSpeculativeStats();
    std::cout << stats << "\n";

    // 6. 计算加速比
    std::cout << "\n🏆 性能总结\n";
    std::cout << "========================================\n";
    std::cout << "贪婪解码延迟:     " << duration1 << " ms\n";
    std::cout << "推测式解码延迟:   " << duration2 << " ms\n";

    if (duration2 > 0) {
        double speedup = static_cast<double>(duration1) / duration2;
        std::cout << "加速比:           " << std::fixed << std::setprecision(2)
                  << speedup << "x\n";

        if (speedup > 1.5) {
            std::cout << "✅ 推测式解码显著加速!\n";
        } else if (speedup > 1.0) {
            std::cout << "✅ 推测式解码略有加速\n";
        } else {
            std::cout << "⚠️  推测式解码未带来加速，可能原因:\n";
            std::cout << "   - 接受率过低 (< 30%)\n";
            std::cout << "   - 温度过高\n";
            std::cout << "   - Draft模型过慢\n";
        }
    }

    std::cout << "========================================\n";
    std::cout << "\n✅ 测试完成!\n";

    return 0;
}
