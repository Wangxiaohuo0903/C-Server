/**
 * @file test_classifier_only.cpp
 * @brief 仅测试TaskClassifier的独立程序（不需要llama.cpp）
 *
 * 编译方式：
 *   g++ -std=c++20 test_classifier_only.cpp src/inference/TaskClassifier.cpp -I. -o test_classifier
 *
 * 运行：
 *   ./test_classifier
 */

#include "src/inference/TaskClassifier.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║        TaskClassifier 独立测试程序                       ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    // 创建分类器
    TaskClassifier classifier(true);  // verbose模式

    // 测试用例
    struct TestCase {
        std::string prompt;
        TaskType expected;
    };

    std::vector<TestCase> tests = {
        {"用Python实现快速排序算法", TaskType::CODE_GENERATION},
        {"Write a C++ binary search function", TaskType::CODE_GENERATION},
        {"生成一个JSON用户配置文件", TaskType::JSON_GENERATION},
        {"Create a JSON API response", TaskType::JSON_GENERATION},
        {"计算 (x+2)(x-3) 的展开式", TaskType::MATH_REASONING},
        {"Solve: 2x + 5 = 15", TaskType::MATH_REASONING},
        {"翻译成英语：今天天气很好", TaskType::TRANSLATION},
        {"Translate to Chinese: Hello World", TaskType::TRANSLATION},
        {"总结以下文章的要点...", TaskType::SUMMARIZATION},
        {"什么是机器学习？", TaskType::QA_CONVERSATION},
        {"写一首关于秋天的诗", TaskType::CREATIVE_WRITING},
        {"Write a short story", TaskType::CREATIVE_WRITING}
    };

    int correct = 0;
    int total = tests.size();

    for (size_t i = 0; i < tests.size(); ++i) {
        const auto& test = tests[i];

        std::cout << "\n【测试 " << (i + 1) << "】\n";
        std::cout << "Prompt: " << test.prompt << "\n";

        auto result = classifier.classify(test.prompt);

        std::cout << "预期: " << taskTypeToString(test.expected) << "\n";
        std::cout << "检测: " << taskTypeToString(result.task_type) << "\n";
        std::cout << "置信度: " << (result.confidence * 100.0f) << "%\n";
        std::cout << "推荐n_draft: " << result.config.n_draft << "\n";

        if (result.task_type == test.expected) {
            std::cout << "✅ 正确\n";
            correct++;
        } else {
            std::cout << "❌ 错误\n";
        }
    }

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║                    测试结果                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";
    std::cout << "总数: " << total << "\n";
    std::cout << "正确: " << correct << "\n";
    std::cout << "准确率: " << std::fixed << std::setprecision(1)
              << (correct * 100.0 / total) << "%\n\n";

    if (correct >= total * 0.8) {
        std::cout << "✅ 测试通过！分类器工作正常。\n";
        return 0;
    } else {
        std::cout << "⚠️ 准确率低于80%，需要检查。\n";
        return 1;
    }
}
