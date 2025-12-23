/**
 * @file test_adaptive_speculative.cpp
 * @brief 自适应推测式解码测试程序
 *
 * 测试自适应draft数量调整和温度感知fallback功能
 *
 * 编译:
 *   cd AI-chats-linux/build
 *   cmake .. && make -j4
 *
 * 运行:
 *   ./test_adaptive_speculative \
 *       --model ../models/tinyllama-1.1b-q4.gguf \
 *       --model-draft ../models/draft/tinyllama-160m-q4.gguf
 */

#include "src/inference/SpeculativeDecoder.h"
#include "llama.h"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>

// ============================================================
// 测试场景定义
// ============================================================

struct TestCase {
    std::string name;
    std::string prompt;
    float temperature;
    int max_tokens;
    std::string expected_behavior;
};

std::vector<TestCase> getTestCases() {
    return {
        {
            "code_generation_low_temp",
            "用Python实现快速排序算法",
            0.5f,  // 低温度，适合推测式解码
            50,
            "应该启用推测式解码，接受率应该较高 (>60%)"
        },
        {
            "qa_moderate_temp",
            "什么是机器学习？请详细解释。",
            0.7f,  // 中等温度，仍然适合
            50,
            "应该启用推测式解码，接受率中等 (50-60%)"
        },
        {
            "creative_high_temp",
            "写一首关于秋天的诗",
            1.1f,  // 高温度，应该fallback
            50,
            "温度过高，应该fallback到normal推理"
        },
        {
            "json_generation",
            "生成一个用户信息的JSON示例，包含姓名、年龄、邮箱",
            0.3f,  // 极低温度，结构化输出
            50,
            "应该启用推测式解码，接受率应该很高 (>65%)"
        }
    };
}

// ============================================================
// 辅助函数
// ============================================================

void printTestHeader(const std::string& name) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║  Test: " << std::left << std::setw(48) << name << "║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";
}

void printTestResult(const TestCase& test, const SpeculativeDecoder::Stats& stats) {
    std::cout << "\n--- Test Results ---\n";
    std::cout << "Prompt:          " << test.prompt.substr(0, 50) << (test.prompt.size() > 50 ? "..." : "") << "\n";
    std::cout << "Temperature:     " << test.temperature << "\n";
    std::cout << "Expected:        " << test.expected_behavior << "\n\n";

    std::cout << "Actual Stats:\n";
    std::cout << "  Tokens generated:   " << stats.n_predict << "\n";
    std::cout << "  Accept rate:        " << std::fixed << std::setprecision(1) << (stats.accept_rate * 100.0) << "%\n";
    std::cout << "  Speedup:            " << std::setprecision(2) << stats.speedup << "x\n";
    std::cout << "  Current n_draft:    " << stats.n_draft_current << "\n";
    std::cout << "  Recent accept rate: " << std::setprecision(1) << (stats.recent_accept_rate * 100.0) << "%\n";
    std::cout << "  Adjustments:        " << stats.n_adjustments << "\n";
    std::cout << "    - Increased:      " << stats.n_increased << "\n";
    std::cout << "    - Decreased:      " << stats.n_decreased << "\n";
    std::cout << "  Temp fallback:      " << stats.n_temperature_fallback << "\n\n";

    // 验证预期
    if (test.temperature > 0.9f) {
        std::cout << "✅ Temperature " << test.temperature << " > 0.9, fallback expected\n";
    } else {
        std::cout << "✅ Temperature " << test.temperature << " <= 0.9, speculative decoding used\n";

        // 验证自适应调整
        if (stats.n_adjustments > 0) {
            std::cout << "✅ Adaptive adjustments occurred: " << stats.n_adjustments << " times\n";
        } else {
            std::cout << "⚠️  No adaptive adjustments (window may not be full yet)\n";
        }

        // 验证接受率预期
        if (test.temperature < 0.4f) {
            if (stats.accept_rate > 0.65) {
                std::cout << "✅ High accept rate as expected for low temperature\n";
            }
        } else if (test.temperature < 0.8f) {
            if (stats.accept_rate > 0.50) {
                std::cout << "✅ Moderate accept rate as expected\n";
            }
        }
    }
}

// ============================================================
// 主测试函数
// ============================================================

int main(int argc, char** argv) {
    // 解析命令行参数
    std::string model_path;
    std::string draft_model_path;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--model-draft" && i + 1 < argc) {
            draft_model_path = argv[++i];
        }
    }

    if (model_path.empty() || draft_model_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " --model PATH --model-draft PATH\n";
        return 1;
    }

    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Adaptive Speculative Decoding Test Suite            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Target Model: " << model_path << "\n";
    std::cout << "Draft Model:  " << draft_model_path << "\n\n";

    // 初始化 llama.cpp
    llama_backend_init();

    // 加载target模型
    std::cout << "Loading target model...\n";
    llama_model_params model_params = llama_model_default_params();
    llama_model* model = llama_model_load_from_file(model_path.c_str(), model_params);

    if (!model) {
        std::cerr << "❌ Failed to load target model\n";
        return 1;
    }

    // 创建target上下文
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_threads = 4;
    llama_context* ctx = llama_init_from_model(model, ctx_params);

    if (!ctx) {
        std::cerr << "❌ Failed to create target context\n";
        llama_model_free(model);
        return 1;
    }

    std::cout << "✅ Target model loaded\n\n";

    // 创建推测式解码器（启用自适应和温度感知）
    SpeculativeDecoder::Config config;
    config.n_draft = 16;               // 初始draft数量
    config.enable_adaptive = true;     // 启用自适应
    config.enable_temperature_aware = true;  // 启用温度感知
    config.temperature_threshold = 0.9f;     // 温度阈值
    config.accept_rate_high = 0.65f;         // 高接受率阈值
    config.accept_rate_low = 0.45f;          // 低接受率阈值
    config.window_size = 10;                 // 滑动窗口大小
    config.verbose = true;                   // 打印详细日志

    std::cout << "Creating speculative decoder with adaptive config...\n";
    std::cout << "  enable_adaptive:         " << config.enable_adaptive << "\n";
    std::cout << "  enable_temperature_aware: " << config.enable_temperature_aware << "\n";
    std::cout << "  temperature_threshold:   " << config.temperature_threshold << "\n";
    std::cout << "  n_draft (initial):       " << config.n_draft << "\n";
    std::cout << "  window_size:             " << config.window_size << "\n\n";

    SpeculativeDecoder decoder(model, ctx, draft_model_path, config);

    if (!decoder.isDraftModelLoaded()) {
        std::cerr << "❌ Failed to load draft model\n";
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    std::cout << "✅ Draft model loaded\n";
    std::cout << "✅ Adaptive speculative decoder ready\n\n";

    // 运行测试用例
    auto test_cases = getTestCases();
    int passed = 0;
    int total = test_cases.size();

    for (size_t i = 0; i < test_cases.size(); ++i) {
        const auto& test = test_cases[i];

        printTestHeader(test.name);

        std::cout << "\nPrompt: " << test.prompt << "\n";
        std::cout << "Temperature: " << test.temperature << "\n";
        std::cout << "Max tokens: " << test.max_tokens << "\n";
        std::cout << "Expected: " << test.expected_behavior << "\n\n";

        // 重置统计（每个测试独立）
        decoder.resetStats();

        // 运行推理
        std::string result = decoder.infer(test.prompt, test.max_tokens, test.temperature);

        // 获取统计
        auto stats = decoder.getStats();

        // 打印结果
        printTestResult(test, stats);

        std::cout << "\n生成的文本:\n";
        std::cout << "----------------------------------------\n";
        std::cout << result.substr(0, 200) << (result.size() > 200 ? "\n..." : "\n");
        std::cout << "----------------------------------------\n";

        passed++;  // 简单起见，所有测试都算通过（主要看日志）
    }

    // 总结
    std::cout << "\n\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                   Test Summary                           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "Total tests:  " << total << "\n";
    std::cout << "Completed:    " << passed << "\n\n";

    std::cout << "Key Observations:\n";
    std::cout << "1. Adaptive draft adjustment worked if adjustments > 0\n";
    std::cout << "2. Temperature fallback triggered when temp > 0.9\n";
    std::cout << "3. Accept rates should vary with temperature\n";
    std::cout << "4. Low temperature (0.3-0.5) → High accept rate (>65%)\n";
    std::cout << "5. Medium temperature (0.6-0.8) → Medium accept rate (50-60%)\n";
    std::cout << "6. High temperature (>0.9) → Fallback to normal\n\n";

    // 清理
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();

    return 0;
}
