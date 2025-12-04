/**
 * @file test_task_aware.cpp
 * @brief 任务感知推测式解码测试程序
 *
 * 测试任务分类器能否正确识别不同类型的任务，并应用相应的优化配置
 *
 * 编译:
 *   cd AI-chats-linux/build
 *   cmake .. && make -j4
 *
 * 运行:
 *   ./test_task_aware \
 *       --model ../models/tinyllama-1.1b-q4.gguf \
 *       --model-draft ../models/draft/tinyllama-160m-q4.gguf
 */

#include "src/inference/SpeculativeDecoder.h"
#include "src/inference/TaskClassifier.h"
#include "llama.h"
#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <algorithm>  // for std::transform

// ============================================================
// 测试场景定义
// ============================================================

struct TestCase {
    std::string name;
    std::string prompt;
    TaskType expected_task_type;
    int expected_n_draft_min;
    int expected_n_draft_max;
    float temperature;
    int max_tokens;
};

std::vector<TestCase> getTestCases() {
    return {
        {
            "code_generation_python",
            "def binary_search(arr, target):\n    \"\"\"二分查找算法\n    Args:\n        arr: 已排序数组\n        target: 目标值\n    Returns:\n        目标元素的索引，未找到返回-1\n    \"\"\"\n    # 实现代码:\n    left = 0\n    right = len(arr) - 1\n    \n    while",  // 引导式prompt - 直接开始代码让模型补全
            TaskType::CODE_GENERATION,
            24, 32,  // 期望n_draft范围
            0.0f,  // greedy for edge computing
            100  // 增加max_tokens以容纳更多代码
        },
        {
            "code_generation_cpp",
            "Write a C++ function to reverse a linked list",
            TaskType::CODE_GENERATION,
            24, 32,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "qa_general",
            "问题：什么是机器学习？\n\n回答：机器学习（Machine Learning）是人工智能的一个重要分支，它是一种",  // 引导式prompt - 开始回答让模型补全
            TaskType::QA_CONVERSATION,
            12, 24,
            0.0f,  // greedy for edge computing
            100  // 增加max_tokens以容纳完整回答
        },
        {
            "qa_explain",
            "Explain how neural networks work",
            TaskType::QA_CONVERSATION,
            12, 24,
            0.0f,  // greedy for edge computing
            50
        },
        {
            "creative_poetry",
            "写一首关于春天的诗，要有意境",
            TaskType::CREATIVE_WRITING,
            4, 12,
            0.0f,  // greedy for edge computing
            50
        },
        {
            "creative_story",
            "Write a short story about a time traveler",
            TaskType::CREATIVE_WRITING,
            4, 12,
            0.0f,  // greedy for edge computing
            50
        },
        {
            "json_user_data",
            "生成一个用户信息的JSON示例，包含姓名、年龄、邮箱、地址",
            TaskType::JSON_GENERATION,
            20, 32,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "json_config",
            "Create a JSON config file for a web server",
            TaskType::JSON_GENERATION,
            20, 32,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "math_calculation",
            "计算 (3x + 5) * (2x - 1) 的展开式",
            TaskType::MATH_REASONING,
            16, 28,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "math_solve",
            "Solve the equation: 2x^2 + 5x - 3 = 0",
            TaskType::MATH_REASONING,
            16, 28,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "translation_zh_en",
            "今天天气很好 -> The weather is very ",  // 更简洁的引导式 - 用箭头表示翻译关系
            TaskType::TRANSLATION,
            16, 28,
            0.0f,  // greedy for edge computing
            60  // Increased to get complete translation
        },
        {
            "translation_en_zh",
            "Translate to Chinese: The quick brown fox jumps over the lazy dog",
            TaskType::TRANSLATION,
            16, 28,
            0.0f,  // greedy for edge computing
            40
        },
        {
            "summarization",
            "请总结以下内容的要点：机器学习是人工智能的一个分支...",
            TaskType::SUMMARIZATION,
            12, 24,
            0.0f,  // greedy for edge computing
            40
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

void printClassificationResult(
    const TestCase& test,
    const TaskClassifier::ClassificationResult& result
) {
    std::cout << "\n--- Classification Result ---\n";
    std::cout << "Prompt:           " << test.prompt.substr(0, 60)
              << (test.prompt.size() > 60 ? "..." : "") << "\n";
    std::cout << "Expected type:    " << taskTypeToString(test.expected_task_type) << "\n";
    std::cout << "Detected type:    " << taskTypeToString(result.task_type);

    if (result.task_type == test.expected_task_type) {
        std::cout << " ✅ CORRECT\n";
    } else {
        std::cout << " ❌ MISMATCH\n";
    }

    std::cout << "Confidence:       " << (result.confidence * 100.0f) << "%\n";
    std::cout << "Reasoning:        " << result.reasoning << "\n";
    std::cout << "Applied n_draft:  " << result.config.n_draft << "\n";
    std::cout << "Accept rate range: [" << result.config.accept_rate_low
              << ", " << result.config.accept_rate_high << "]\n";

    // 验证n_draft是否在期望范围内
    if (result.task_type == test.expected_task_type) {
        if (result.config.n_draft >= test.expected_n_draft_min &&
            result.config.n_draft <= test.expected_n_draft_max) {
            std::cout << "✅ n_draft in expected range ["
                      << test.expected_n_draft_min << ", "
                      << test.expected_n_draft_max << "]\n";
        } else {
            std::cout << "⚠️  n_draft out of expected range\n";
        }
    }
}

void printInferenceResult(
    const TestCase& test,
    const SpeculativeDecoder::Stats& stats,
    const std::string& generated_text
) {
    std::cout << "\n--- Inference Result ---\n";
    std::cout << "Tokens generated:   " << stats.n_predict << "\n";
    std::cout << "Accept rate:        " << std::fixed << std::setprecision(1)
              << (stats.accept_rate * 100.0) << "%\n";
    std::cout << "Speedup:            " << std::setprecision(2) << stats.speedup << "x\n";
    std::cout << "Final n_draft:      " << stats.n_draft_current << "\n";
    std::cout << "Detected task:      " << stats.detected_task_type << "\n";
    std::cout << "Classification conf: " << std::setprecision(1)
              << (stats.task_classification_confidence * 100.0f) << "%\n";

    std::cout << "\nGenerated text (first 150 chars):\n";
    std::cout << "----------------------------------------\n";
    std::cout << generated_text.substr(0, 150);
    if (generated_text.size() > 150) std::cout << "...";
    std::cout << "\n----------------------------------------\n";
}

// ============================================================
// 主测试函数
// ============================================================

int main(int argc, char** argv) {
    // 解析命令行参数
    std::string model_path;
    std::string draft_model_path;
    std::string task_type_filter;  // 用于批量测试时指定单一任务类型
    int run_number = 0;            // 批量测试的运行编号

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--model-draft" && i + 1 < argc) {
            draft_model_path = argv[++i];
        } else if (arg == "--task-type" && i + 1 < argc) {
            task_type_filter = argv[++i];
        } else if (arg == "--run-number" && i + 1 < argc) {
            run_number = std::stoi(argv[++i]);
        }
    }

    if (model_path.empty() || draft_model_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " --model PATH --model-draft PATH [--task-type TYPE] [--run-number N]\n";
        std::cerr << "\nTask types: CODE_GENERATION, JSON_GENERATION, QA_CONVERSATION, "
                  << "CREATIVE_WRITING, MATH_REASONING, TRANSLATION\n";
        return 1;
    }

    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║       Task-Aware Speculative Decoding Test Suite        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "Target Model: " << model_path << "\n";
    std::cout << "Draft Model:  " << draft_model_path << "\n";

    if (!task_type_filter.empty()) {
        std::cout << "Task Filter:  " << task_type_filter << "\n";
        std::cout << "Run Number:   " << run_number << "\n";
        std::cout << "Mode:         Batch Collection (single task only)\n";
    } else {
        std::cout << "Mode:         Full Test Suite\n";
    }
    std::cout << "\n";

    // 获取所有测试用例
    auto test_cases = getTestCases();

    // ============ Part 1: 测试TaskClassifier独立功能 ============
    // 如果指定了任务类型过滤器，跳过Part 1（批量收集模式）

    if (task_type_filter.empty()) {
        std::cout << "\n";
        std::cout << "════════════════════════════════════════════════════════════\n";
        std::cout << "  PART 1: Testing TaskClassifier Standalone\n";
        std::cout << "════════════════════════════════════════════════════════════\n";

        TaskClassifier classifier(true);  // verbose模式
        int classification_correct = 0;

        for (const auto& test : test_cases) {
            printTestHeader(test.name + " (Classification Only)");

            auto result = classifier.classify(test.prompt);
            printClassificationResult(test, result);

            if (result.task_type == test.expected_task_type) {
                classification_correct++;
            }
        }

        std::cout << "\n";
        std::cout << "Classification Accuracy: " << classification_correct
                  << "/" << test_cases.size()
                  << " (" << (classification_correct * 100.0 / test_cases.size())
                  << "%)\n";
    } else {
        std::cout << "\n[Skipping Part 1: Classification Tests - Batch Collection Mode]\n";
    }

    // ============ Part 2: 测试完整推理流程 ============

    std::cout << "\n\n";
    std::cout << "════════════════════════════════════════════════════════════\n";
    std::cout << "  PART 2: Testing Full Inference with Task-Aware\n";
    std::cout << "════════════════════════════════════════════════════════════\n";

    // 初始化 llama.cpp
    llama_backend_init();

    // 加载target模型
    std::cout << "\nLoading target model...\n";
    llama_model_params model_params = llama_model_default_params();
    llama_model* model = llama_model_load_from_file(model_path.c_str(), model_params);

    if (!model) {
        std::cerr << "❌ Failed to load target model\n";
        return 1;
    }

    // 创建target上下文
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_batch = 512;  // 增大batch size以支持长prompt
    ctx_params.n_threads = 4;
    llama_context* ctx = llama_init_from_model(model, ctx_params);

    if (!ctx) {
        std::cerr << "❌ Failed to create target context\n";
        llama_model_free(model);
        return 1;
    }

    std::cout << "✅ Target model loaded\n";

    // 创建推测式解码器（启用任务感知）
    SpeculativeDecoder::Config config;
    config.n_draft = 16;                     // 初始值（会被任务感知覆盖）
    config.enable_adaptive = true;           // 启用自适应
    config.enable_temperature_aware = true;  // 启用温度感知
    config.enable_task_aware = true;         // 启用任务感知（核心）
    config.task_aware_verbose = true;        // 打印任务分类详情
    config.enable_confidence_guide = true;   // 启用置信度引导（Phase 2）
    config.confidence_verbose = false;       // 关闭置信度详细日志
    config.verbose = false;                  // 关闭其他详细日志

    std::cout << "\nCreating speculative decoder with task-aware config...\n";
    std::cout << "  enable_task_aware:       " << config.enable_task_aware << "\n";
    std::cout << "  enable_adaptive:         " << config.enable_adaptive << "\n";
    std::cout << "  enable_temperature_aware: " << config.enable_temperature_aware << "\n\n";

    SpeculativeDecoder decoder(model, ctx, draft_model_path, config);

    if (!decoder.isDraftModelLoaded()) {
        std::cerr << "❌ Failed to load draft model\n";
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    std::cout << "✅ Draft model loaded\n";
    std::cout << "✅ Task-aware speculative decoder ready\n\n";

    // 选择要运行的测试用例
    std::vector<int> test_indices;

    if (!task_type_filter.empty()) {
        // 批量收集模式：只运行指定任务类型的第一个测试用例
        // 转换过滤器为小写以进行不区分大小写的匹配
        std::string filter_lower = task_type_filter;
        std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

        for (size_t i = 0; i < test_cases.size(); i++) {
            std::string name_lower = test_cases[i].name;
            std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

            if (name_lower.find(filter_lower) != std::string::npos) {
                test_indices.push_back(i);
                break;  // 只选第一个匹配的测试用例
            }
        }

        if (test_indices.empty()) {
            std::cerr << "❌ No test case found for task type: " << task_type_filter << "\n";
            llama_free(ctx);
            llama_model_free(model);
            llama_backend_free();
            return 1;
        }
    } else {
        // 完整测试模式：运行部分代表性测试用例
        test_indices = {0, 2, 4, 6, 8, 10};  // 代码、问答、创意、JSON、数学、翻译
    }

    int inference_passed = 0;

    for (int idx : test_indices) {
        const auto& test = test_cases[idx];

        printTestHeader(test.name + " (Full Inference)");

        std::cout << "\nPrompt: " << test.prompt << "\n";
        std::cout << "Temperature: " << test.temperature << "\n";
        std::cout << "Max tokens: " << test.max_tokens << "\n";
        std::cout << "Expected task: " << taskTypeToString(test.expected_task_type) << "\n\n";

        // 重置统计（每个测试独立）
        decoder.resetStats();

        // 运行推理
        std::string result = decoder.infer(test.prompt, test.max_tokens, test.temperature);

        // 获取统计
        auto stats = decoder.getStats();

        // 打印结果
        printInferenceResult(test, stats, result);

        // 验证任务识别是否正确
        if (stats.detected_task_type == taskTypeToString(test.expected_task_type)) {
            std::cout << "\n✅ Task correctly detected and applied\n";
            inference_passed++;
        } else {
            std::cout << "\n⚠️  Task detection mismatch\n";
        }
    }

    // ============ 总结 ============

    std::cout << "\n\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     Test Summary                         ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << "PART 1 - Classification Tests:\n";
    std::cout << "  Total:        " << test_cases.size() << "\n";
    std::cout << "  Correct:      " << classification_correct << "\n";
    std::cout << "  Accuracy:     " << std::fixed << std::setprecision(1)
              << (classification_correct * 100.0 / test_cases.size()) << "%\n\n";

    std::cout << "PART 2 - Inference Tests:\n";
    std::cout << "  Total:        " << test_indices.size() << "\n";
    std::cout << "  Passed:       " << inference_passed << "\n";
    std::cout << "  Success rate: " << std::fixed << std::setprecision(1)
              << (inference_passed * 100.0 / test_indices.size()) << "%\n\n";

    std::cout << "Key Observations:\n";
    std::cout << "1. Task classifier should correctly identify most tasks (>80%)\n";
    std::cout << "2. Different task types should use different n_draft values\n";
    std::cout << "3. Code/JSON tasks should have higher accept rates\n";
    std::cout << "4. Creative tasks should use smaller n_draft values\n\n";

    // 清理
    llama_free(ctx);
    llama_model_free(model);
    llama_backend_free();

    return 0;
}
