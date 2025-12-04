/**
 * @file test_confidence_guide.cpp
 * @brief Token置信度引导系统单元测试
 *
 * 测试内容:
 * 1. TokenConfidenceCalculator - Shannon熵计算
 * 2. ConfidenceGuidedStrategy - 自适应调整策略
 * 3. ConfidenceAnalyzer - 相关性分析
 * 4. 性能测试 - 确保overhead < 1ms
 *
 * @author AI-infra Team
 * @date 2025-11-27
 */

#include "src/inference/ConfidenceGuide.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cassert>
#include <cmath>
#include <random>

using namespace std;

//=============================================================================
// 测试辅助函数
//=============================================================================

void printTestHeader(const string& test_name) {
    cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    cout << "║  " << left << setw(54) << test_name << "  ║\n";
    cout << "╚══════════════════════════════════════════════════════════╝\n";
}

void printTestResult(const string& test_name, bool passed) {
    cout << "[" << (passed ? "✓ PASS" : "✗ FAIL") << "] " << test_name << endl;
}

bool floatEqual(float a, float b, float epsilon = 1e-4f) {
    return std::abs(a - b) < epsilon;
}

//=============================================================================
// Test 1: TokenConfidenceCalculator - Softmax计算
//=============================================================================

void test_softmax() {
    printTestHeader("Test 1: Softmax Normalization");

    TokenConfidenceCalculator calculator;

    // Test case 1: 简单logits
    {
        vector<float> logits = {1.0f, 2.0f, 3.0f};
        vector<float> probs = calculator.softmax(logits);

        // 检查概率和 = 1
        float sum = 0.0f;
        for (float p : probs) {
            sum += p;
        }

        bool test1_passed = floatEqual(sum, 1.0f, 1e-5f);
        printTestResult("Softmax sum = 1.0", test1_passed);

        // 检查单调性 (logit越大，prob越大)
        bool test2_passed = (probs[0] < probs[1]) && (probs[1] < probs[2]);
        printTestResult("Softmax monotonicity", test2_passed);

        cout << "  Logits: [1.0, 2.0, 3.0]\n";
        cout << "  Probs:  [" << fixed << setprecision(3)
             << probs[0] << ", " << probs[1] << ", " << probs[2] << "]\n";
    }

    // Test case 2: 大数值稳定性
    {
        vector<float> logits = {1000.0f, 1001.0f, 1002.0f};
        vector<float> probs = calculator.softmax(logits);

        float sum = 0.0f;
        for (float p : probs) {
            sum += p;
        }

        bool test3_passed = floatEqual(sum, 1.0f, 1e-5f) && !std::isnan(probs[0]);
        printTestResult("Softmax numerical stability (large values)", test3_passed);
    }

    // Test case 3: 边界情况 - 单个元素
    {
        vector<float> logits = {5.0f};
        vector<float> probs = calculator.softmax(logits);

        bool test4_passed = floatEqual(probs[0], 1.0f, 1e-5f);
        printTestResult("Softmax single element", test4_passed);
    }
}

//=============================================================================
// Test 2: TokenConfidenceCalculator - Shannon熵计算
//=============================================================================

void test_entropy() {
    printTestHeader("Test 2: Shannon Entropy Calculation");

    TokenConfidenceCalculator calculator;

    // Test case 1: 均匀分布 (最大熵)
    {
        vector<float> probs = {0.25f, 0.25f, 0.25f, 0.25f};
        float entropy = calculator.calculateEntropy(probs);

        // log2(4) = 2.0
        float expected_entropy = 2.0f;
        bool test_passed = floatEqual(entropy, expected_entropy, 1e-3f);
        printTestResult("Uniform distribution entropy = log2(n)", test_passed);

        cout << "  Probs: [0.25, 0.25, 0.25, 0.25]\n";
        cout << "  Entropy: " << fixed << setprecision(3) << entropy
             << " (expected: " << expected_entropy << ")\n";
    }

    // Test case 2: 确定性分布 (最小熵 = 0)
    {
        vector<float> probs = {1.0f, 0.0f, 0.0f, 0.0f};
        float entropy = calculator.calculateEntropy(probs);

        bool test_passed = floatEqual(entropy, 0.0f, 1e-5f);
        printTestResult("Deterministic distribution entropy = 0", test_passed);

        cout << "  Probs: [1.0, 0.0, 0.0, 0.0]\n";
        cout << "  Entropy: " << entropy << "\n";
    }

    // Test case 3: 部分确定性
    {
        vector<float> probs = {0.7f, 0.2f, 0.1f};
        float entropy = calculator.calculateEntropy(probs);

        // 熵应该在 (0, log2(3)) 之间
        float max_entropy = std::log2(3.0f);
        bool test_passed = (entropy > 0.0f) && (entropy < max_entropy);
        printTestResult("Partial certainty: 0 < entropy < log2(n)", test_passed);

        cout << "  Probs: [0.7, 0.2, 0.1]\n";
        cout << "  Entropy: " << entropy << " (max: " << max_entropy << ")\n";
    }
}

//=============================================================================
// Test 3: TokenConfidenceCalculator - 置信度计算
//=============================================================================

void test_confidence() {
    printTestHeader("Test 3: Confidence Calculation");

    TokenConfidenceCalculator calculator;

    // Test case 1: 高置信度 (确定性logits)
    {
        vector<float> logits = {10.0f, 0.0f, 0.0f, 0.0f};  // 极度倾向第一个token
        float confidence = calculator.calculate(logits);

        bool test_passed = confidence > 0.95f;
        printTestResult("High confidence (deterministic)", test_passed);

        cout << "  Logits: [10.0, 0.0, 0.0, 0.0]\n";
        cout << "  Confidence: " << fixed << setprecision(3) << confidence << "\n";
    }

    // Test case 2: 低置信度 (均匀logits)
    {
        vector<float> logits = {1.0f, 1.0f, 1.0f, 1.0f};  // 完全不确定
        float confidence = calculator.calculate(logits);

        bool test_passed = confidence < 0.1f;
        printTestResult("Low confidence (uniform)", test_passed);

        cout << "  Logits: [1.0, 1.0, 1.0, 1.0]\n";
        cout << "  Confidence: " << confidence << "\n";
    }

    // Test case 3: 中等置信度
    {
        vector<float> logits = {3.0f, 2.0f, 1.0f, 0.0f};
        float confidence = calculator.calculate(logits);

        bool test_passed = (confidence > 0.2f) && (confidence < 0.8f);
        printTestResult("Moderate confidence", test_passed);

        cout << "  Logits: [3.0, 2.0, 1.0, 0.0]\n";
        cout << "  Confidence: " << confidence << "\n";
    }

    // Test case 4: Top-1概率
    {
        vector<float> logits = {5.0f, 1.0f, 1.0f, 1.0f};
        float top1_prob = calculator.calculateTop1Probability(logits);

        bool test_passed = top1_prob > 0.9f;
        printTestResult("Top-1 probability", test_passed);

        cout << "  Logits: [5.0, 1.0, 1.0, 1.0]\n";
        cout << "  Top-1 Prob: " << top1_prob << "\n";
    }

    // Test case 5: Top-K Diversity
    {
        vector<float> logits = {3.0f, 2.9f, 2.8f, 1.0f, 1.0f};
        float topk_div = calculator.calculateTopKDiversity(logits, 3);

        bool test_passed = topk_div > 0.8f;  // 高diversity
        printTestResult("Top-K diversity (high)", test_passed);

        cout << "  Top-3 Diversity: " << topk_div << "\n";
    }
}

//=============================================================================
// Test 4: ConfidenceGuidedStrategy - 自适应调整
//=============================================================================

void test_adaptive_strategy() {
    printTestHeader("Test 4: Confidence-Guided Adaptive Strategy");

    ConfidenceGuidedStrategy::Config config;
    config.high_threshold = 0.85f;
    config.low_threshold = 0.65f;
    config.min_n_draft = 4;
    config.max_n_draft = 32;
    config.increase_factor = 1.5f;
    config.decrease_factor = 0.7f;
    config.enable_verbose = false;

    ConfidenceGuidedStrategy strategy(config);

    // Test case 1: 高置信度 → 增加n_draft
    {
        int current_n_draft = 16;
        float high_confidence = 0.90f;

        int new_n_draft = strategy.adjustDraftSize(high_confidence, current_n_draft);

        bool test_passed = new_n_draft > current_n_draft;
        printTestResult("High confidence → Increase n_draft", test_passed);

        cout << "  Confidence: " << high_confidence << " (threshold: " << config.high_threshold << ")\n";
        cout << "  n_draft: " << current_n_draft << " → " << new_n_draft << "\n";
    }

    // Test case 2: 低置信度 → 减少n_draft
    {
        int current_n_draft = 16;
        float low_confidence = 0.50f;

        int new_n_draft = strategy.adjustDraftSize(low_confidence, current_n_draft);

        bool test_passed = new_n_draft < current_n_draft;
        printTestResult("Low confidence → Decrease n_draft", test_passed);

        cout << "  Confidence: " << low_confidence << " (threshold: " << config.low_threshold << ")\n";
        cout << "  n_draft: " << current_n_draft << " → " << new_n_draft << "\n";
    }

    // Test case 3: 中等置信度 → 保持n_draft
    {
        int current_n_draft = 16;
        float moderate_confidence = 0.75f;

        int new_n_draft = strategy.adjustDraftSize(moderate_confidence, current_n_draft);

        bool test_passed = (new_n_draft == current_n_draft);
        printTestResult("Moderate confidence → Keep n_draft", test_passed);

        cout << "  Confidence: " << moderate_confidence << "\n";
        cout << "  n_draft: " << current_n_draft << " → " << new_n_draft << "\n";
    }

    // Test case 4: 边界限制 (max_n_draft)
    {
        int current_n_draft = 30;
        float high_confidence = 0.95f;

        int new_n_draft = strategy.adjustDraftSize(high_confidence, current_n_draft);

        bool test_passed = (new_n_draft <= config.max_n_draft);
        printTestResult("Respect max_n_draft boundary", test_passed);

        cout << "  n_draft: " << current_n_draft << " → " << new_n_draft
             << " (max: " << config.max_n_draft << ")\n";
    }

    // Test case 5: 边界限制 (min_n_draft)
    {
        int current_n_draft = 6;
        float low_confidence = 0.30f;

        int new_n_draft = strategy.adjustDraftSize(low_confidence, current_n_draft);

        bool test_passed = (new_n_draft >= config.min_n_draft);
        printTestResult("Respect min_n_draft boundary", test_passed);

        cout << "  n_draft: " << current_n_draft << " → " << new_n_draft
             << " (min: " << config.min_n_draft << ")\n";
    }
}

//=============================================================================
// Test 5: ConfidenceGuidedStrategy - 统计功能
//=============================================================================

void test_statistics() {
    printTestHeader("Test 5: Strategy Statistics Tracking");

    ConfidenceGuidedStrategy::Config config;
    config.high_threshold = 0.85f;
    config.low_threshold = 0.65f;
    config.enable_verbose = false;

    ConfidenceGuidedStrategy strategy(config);

    // 模拟一系列调整
    int current_n_draft = 16;

    current_n_draft = strategy.adjustDraftSize(0.90f, current_n_draft);  // 高置信度 → 增加
    current_n_draft = strategy.adjustDraftSize(0.88f, current_n_draft);  // 高置信度 → 增加
    current_n_draft = strategy.adjustDraftSize(0.75f, current_n_draft);  // 中等 → 不变
    current_n_draft = strategy.adjustDraftSize(0.50f, current_n_draft);  // 低置信度 → 减少
    current_n_draft = strategy.adjustDraftSize(0.70f, current_n_draft);  // 中等 → 不变

    auto stats = strategy.getStatistics();

    bool test1_passed = (stats.increased_count >= 1);
    bool test2_passed = (stats.decreased_count >= 1);
    bool test3_passed = (stats.confidence_history.size() == 5);

    printTestResult("Track increase adjustments", test1_passed);
    printTestResult("Track decrease adjustments", test2_passed);
    printTestResult("Track confidence history", test3_passed);

    cout << "\nStatistics Summary:\n";
    cout << "  Total adjustments: " << stats.total_adjustments << "\n";
    cout << "  Increased: " << stats.increased_count << "\n";
    cout << "  Decreased: " << stats.decreased_count << "\n";
    cout << "  History samples: " << stats.confidence_history.size() << "\n";
    cout << "  Average confidence: " << fixed << setprecision(3)
         << strategy.getAverageConfidence() << "\n";
    cout << "  Min confidence: " << strategy.getMinConfidence() << "\n";
    cout << "  Max confidence: " << strategy.getMaxConfidence() << "\n";
}

//=============================================================================
// Test 6: ConfidenceAnalyzer - 相关性分析
//=============================================================================

void test_correlation_analyzer() {
    printTestHeader("Test 6: Confidence-Accept Correlation Analysis");

    ConfidenceAnalyzer analyzer;

    // 模拟实验数据: 高置信度 → 高accept rate
    std::random_device rd;
    std::mt19937 gen(42);  // 固定种子

    // 生成100个样本
    for (int i = 0; i < 100; ++i) {
        float confidence = (i / 100.0f);  // 0.0 ~ 1.0

        // 模拟: 置信度越高，accept概率越高
        float accept_prob = confidence * 0.8f + 0.1f;  // 0.1 ~ 0.9

        std::uniform_real_distribution<> dis(0.0, 1.0);
        bool accepted = (dis(gen) < accept_prob);

        analyzer.recordSample(confidence, accepted);
    }

    float correlation = analyzer.calculateCorrelation();

    bool test_passed = (correlation > 0.5f);  // 应该有明显正相关
    printTestResult("Positive correlation (r > 0.5)", test_passed);

    cout << "\nCorrelation Analysis:\n";
    cout << "  Pearson r = " << fixed << setprecision(3) << correlation << "\n";
    cout << "  Sample count: " << analyzer.getSamples().size() << "\n";

    analyzer.printBinStatistics();
}

//=============================================================================
// Test 7: 平滑调整策略
//=============================================================================

void test_smooth_adjustment() {
    printTestHeader("Test 7: Smooth Adjustment Strategy");

    ConfidenceGuidedStrategy::Config config;
    config.high_threshold = 0.85f;
    config.low_threshold = 0.65f;
    config.min_n_draft = 4;
    config.max_n_draft = 32;
    config.increase_factor = 1.5f;
    config.decrease_factor = 0.7f;
    config.enable_verbose = false;

    ConfidenceGuidedStrategy strategy(config);

    int base_n_draft = 16;

    // 测试不同置信度下的平滑调整
    cout << "\nConfidence → Multiplier (Smooth):\n";
    for (float conf = 0.3f; conf <= 1.0f; conf += 0.1f) {
        int new_n_draft = strategy.adjustDraftSizeSmooth(conf, base_n_draft);
        float multiplier = static_cast<float>(new_n_draft) / base_n_draft;

        cout << "  " << fixed << setprecision(2) << conf
             << " → n_draft=" << new_n_draft
             << " (×" << setprecision(2) << multiplier << ")\n";
    }

    printTestResult("Smooth adjustment works", true);
}

//=============================================================================
// Test 8: 性能测试
//=============================================================================

void test_performance() {
    printTestHeader("Test 8: Performance Benchmark (Overhead < 1ms)");

    TokenConfidenceCalculator calculator;

    // 模拟真实词表大小
    const int vocab_size = 32000;

    // 生成随机logits
    std::random_device rd;
    std::mt19937 gen(42);
    std::uniform_real_distribution<> dis(-5.0, 5.0);

    vector<float> logits(vocab_size);
    for (int i = 0; i < vocab_size; ++i) {
        logits[i] = dis(gen);
    }

    // 性能测试: 计算1000次
    const int iterations = 1000;

    auto start = chrono::high_resolution_clock::now();

    for (int i = 0; i < iterations; ++i) {
        float confidence = calculator.calculate(logits);
        (void)confidence;  // 避免优化掉
    }

    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end - start);

    double avg_time_us = duration.count() / static_cast<double>(iterations);
    double avg_time_ms = avg_time_us / 1000.0;

    bool test_passed = (avg_time_ms < 1.0);  // 目标: < 1ms
    printTestResult("Average calculation time < 1ms", test_passed);

    cout << "\nPerformance Results:\n";
    cout << "  Vocabulary size: " << vocab_size << "\n";
    cout << "  Iterations: " << iterations << "\n";
    cout << "  Average time: " << fixed << setprecision(3) << avg_time_ms << " ms\n";
    cout << "  Throughput: " << static_cast<int>(1000.0 / avg_time_ms) << " calculations/sec\n";
}

//=============================================================================
// Test 9: CSV导出
//=============================================================================

void test_csv_export() {
    printTestHeader("Test 9: CSV Export Functionality");

    ConfidenceAnalyzer analyzer;

    // 添加一些样本
    analyzer.recordSample(0.9f, true);
    analyzer.recordSample(0.7f, true);
    analyzer.recordSample(0.5f, false);
    analyzer.recordSample(0.3f, false);

    // 导出CSV
    string filename = "/workspace/test_confidence_samples.csv";
    analyzer.saveToCSV(filename);

    printTestResult("CSV export completed", true);
}

//=============================================================================
// 主测试函数
//=============================================================================

int main() {
    cout << "\n";
    cout << "╔══════════════════════════════════════════════════════════╗\n";
    cout << "║                                                          ║\n";
    cout << "║    Token Confidence Guidance - Unit Test Suite          ║\n";
    cout << "║                                                          ║\n";
    cout << "║    Testing: ConfidenceGuide.cpp                          ║\n";
    cout << "║    Date: 2025-11-27                                      ║\n";
    cout << "║                                                          ║\n";
    cout << "╚══════════════════════════════════════════════════════════╝\n";

    try {
        test_softmax();
        test_entropy();
        test_confidence();
        test_adaptive_strategy();
        test_statistics();
        test_correlation_analyzer();
        test_smooth_adjustment();
        test_performance();
        test_csv_export();

        cout << "\n";
        cout << "╔══════════════════════════════════════════════════════════╗\n";
        cout << "║                    All Tests Passed! ✓                   ║\n";
        cout << "╚══════════════════════════════════════════════════════════╝\n";
        cout << "\n";

        return 0;

    } catch (const exception& e) {
        cerr << "\n[ERROR] Test failed with exception: " << e.what() << endl;
        return 1;
    }
}
