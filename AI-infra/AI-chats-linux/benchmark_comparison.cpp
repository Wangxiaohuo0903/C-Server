/**
 * @file benchmark_comparison.cpp
 * @brief 性能对比测试：传统自回归 vs 推测式解码
 *
 * 编译:
 *   cd AI-chats-linux/build
 *   cmake .. && make -j4
 *
 * 运行:
 *   ./benchmark_comparison \
 *       --model ../models/tinyllama-1.1b-q4.gguf \
 *       --model-draft ../models/draft/tinyllama-160m-q4.gguf \
 *       --output benchmark_results.json
 */

#include "src/inference/ModelManager.h"
#include "llama.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <chrono>
#include <iomanip>
#include <nlohmann/json.hpp>  // 需要 JSON 库

using json = nlohmann::json;

// ============================================================
// 测试场景定义
// ============================================================

struct TestScenario {
    std::string name;
    std::string description;
    std::vector<std::string> prompts;
};

// 获取所有测试场景
std::vector<TestScenario> get_test_scenarios() {
    return {
        {
            "code_generation",
            "代码生成场景（高可预测性）",
            {
                "用Python实现快速排序算法",
                "用C++实现二叉搜索树",
                "用JavaScript实现防抖函数",
                "用Java实现单例模式",
                "用Python实现斐波那契数列"
            }
        },
        {
            "qa_conversation",
            "对话问答场景（中等可预测性）",
            {
                "什么是机器学习？",
                "解释一下什么是深度学习",
                "神经网络是如何工作的？",
                "什么是反向传播算法？",
                "如何防止过拟合？"
            }
        },
        {
            "creative_writing",
            "创意写作场景（低可预测性）",
            {
                "写一首关于秋天的诗",
                "编写一个科幻短篇故事开头",
                "描述一个未来城市的场景",
                "写一段关于友情的感悟",
                "创作一个冒险故事的开篇"
            }
        },
        {
            "json_generation",
            "结构化输出场景（高可预测性）",
            {
                "生成一个用户信息的JSON示例",
                "创建一个产品数据的JSON格式",
                "生成一个API响应的JSON结构",
                "创建一个配置文件的JSON示例",
                "生成一个学生信息的JSON数据"
            }
        }
    };
}

// ============================================================
// 性能测试结果
// ============================================================

struct BenchmarkResult {
    std::string scenario_name;
    std::string prompt;

    // 传统方式
    double normal_time_ms = 0.0;
    int normal_tokens = 0;
    double normal_tokens_per_sec = 0.0;

    // 推测式解码
    double spec_time_ms = 0.0;
    int spec_tokens = 0;
    int spec_drafted = 0;
    int spec_accepted = 0;
    double spec_accept_rate = 0.0;
    double spec_tokens_per_sec = 0.0;
    double speedup = 0.0;

    // 生成的文本
    std::string normal_output;
    std::string spec_output;
};

// ============================================================
// 辅助函数
// ============================================================

// 计时器
class Timer {
public:
    void start() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    double elapsedMs() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// 打印进度条
void print_progress(int current, int total, const std::string& task) {
    int bar_width = 50;
    float progress = (float)current / total;
    int pos = bar_width * progress;

    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos) std::cout << "█";
        else std::cout << " ";
    }
    std::cout << "] " << int(progress * 100.0) << "% - " << task;
    std::cout.flush();

    if (current == total) std::cout << std::endl;
}

// ============================================================
// 主测试函数
// ============================================================

std::vector<BenchmarkResult> run_benchmark(
    ModelManager& manager,
    const std::vector<TestScenario>& scenarios,
    int max_tokens = 100,
    float temperature = 0.7f
) {
    std::vector<BenchmarkResult> results;

    // 计算总测试数
    int total_tests = 0;
    for (const auto& scenario : scenarios) {
        total_tests += scenario.prompts.size();
    }
    total_tests *= 2;  // 每个 prompt 测试两次（normal + speculative）

    int current_test = 0;

    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Performance Benchmark: Normal vs Speculative       ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "Total tests: " << total_tests << " (" << (total_tests / 2) << " prompts × 2 methods)\n";
    std::cout << "\n";

    for (const auto& scenario : scenarios) {
        std::cout << "\n📊 Scenario: " << scenario.name << "\n";
        std::cout << "   " << scenario.description << "\n";
        std::cout << "   Prompts: " << scenario.prompts.size() << "\n";
        std::cout << "\n";

        for (size_t i = 0; i < scenario.prompts.size(); ++i) {
            const auto& prompt = scenario.prompts[i];

            BenchmarkResult result;
            result.scenario_name = scenario.name;
            result.prompt = prompt;

            // ============ 测试 1: 传统方式 ============
            {
                print_progress(++current_test, total_tests,
                              "Normal [" + std::to_string(i+1) + "/" + std::to_string(scenario.prompts.size()) + "]");

                // 禁用推测式解码
                manager.setSpeculativeMode(false);

                Timer timer;
                timer.start();

                std::string chat_id = "benchmark_normal_" + std::to_string(current_test);
                result.normal_output = manager.infer(chat_id, prompt, max_tokens, temperature);

                result.normal_time_ms = timer.elapsedMs();

                // 估算 token 数（简化：假设平均 4 chars/token）
                result.normal_tokens = result.normal_output.size() / 4;
                result.normal_tokens_per_sec = (result.normal_tokens * 1000.0) / result.normal_time_ms;

                // 清理会话
                manager.dropSession(chat_id);
            }

            // ============ 测试 2: 推测式解码 ============
            {
                print_progress(++current_test, total_tests,
                              "Speculative [" + std::to_string(i+1) + "/" + std::to_string(scenario.prompts.size()) + "]");

                // 启用推测式解码
                manager.setSpeculativeMode(true);

                Timer timer;
                timer.start();

                std::string chat_id = "benchmark_spec_" + std::to_string(current_test);
                result.spec_output = manager.inferSpeculative(chat_id, prompt, max_tokens, temperature);

                result.spec_time_ms = timer.elapsedMs();

                // 获取推测式解码统计
                auto stats = manager.getSpeculativeStats();
                result.spec_tokens = stats.n_predict;
                result.spec_drafted = stats.n_drafted;
                result.spec_accepted = stats.n_accepted;
                result.spec_accept_rate = stats.accept_rate;
                result.spec_tokens_per_sec = (result.spec_tokens * 1000.0) / result.spec_time_ms;

                // 计算加速比
                result.speedup = result.normal_time_ms / result.spec_time_ms;

                // 清理会话
                manager.dropSession(chat_id);
            }

            results.push_back(result);
        }
    }

    std::cout << "\n✅ Benchmark complete!\n\n";

    return results;
}

// ============================================================
// 结果分析与输出
// ============================================================

void analyze_and_print_results(const std::vector<BenchmarkResult>& results) {
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                     Benchmark Results                      ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    // 按场景分组统计
    std::map<std::string, std::vector<BenchmarkResult>> by_scenario;
    for (const auto& r : results) {
        by_scenario[r.scenario_name].push_back(r);
    }

    for (const auto& [scenario, scenario_results] : by_scenario) {
        std::cout << "📊 Scenario: " << scenario << "\n";
        std::cout << "   Tests: " << scenario_results.size() << "\n";
        std::cout << "\n";

        // 计算平均值
        double avg_accept_rate = 0.0;
        double avg_speedup = 0.0;
        double avg_normal_tps = 0.0;
        double avg_spec_tps = 0.0;

        for (const auto& r : scenario_results) {
            avg_accept_rate += r.spec_accept_rate;
            avg_speedup += r.speedup;
            avg_normal_tps += r.normal_tokens_per_sec;
            avg_spec_tps += r.spec_tokens_per_sec;
        }

        int n = scenario_results.size();
        avg_accept_rate /= n;
        avg_speedup /= n;
        avg_normal_tps /= n;
        avg_spec_tps /= n;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "   Average Accept Rate:    " << (avg_accept_rate * 100) << "%\n";
        std::cout << "   Average Speedup:         " << avg_speedup << "x\n";
        std::cout << "   Normal Tokens/sec:       " << avg_normal_tps << "\n";
        std::cout << "   Speculative Tokens/sec:  " << avg_spec_tps << "\n";
        std::cout << "\n";

        // 打印详细结果表格
        std::cout << "   Detailed Results:\n";
        std::cout << "   " << std::string(80, '-') << "\n";
        std::cout << "   " << std::setw(30) << std::left << "Prompt"
                  << std::setw(12) << "Accept%"
                  << std::setw(12) << "Speedup"
                  << std::setw(12) << "Normal(ms)"
                  << std::setw(12) << "Spec(ms)\n";
        std::cout << "   " << std::string(80, '-') << "\n";

        for (const auto& r : scenario_results) {
            std::string short_prompt = r.prompt.substr(0, 27);
            if (r.prompt.size() > 27) short_prompt += "...";

            std::cout << "   " << std::setw(30) << std::left << short_prompt
                      << std::setw(12) << std::fixed << std::setprecision(1) << (r.spec_accept_rate * 100)
                      << std::setw(12) << std::setprecision(2) << r.speedup
                      << std::setw(12) << std::setprecision(1) << r.normal_time_ms
                      << std::setw(12) << std::setprecision(1) << r.spec_time_ms << "\n";
        }

        std::cout << "   " << std::string(80, '-') << "\n";
        std::cout << "\n";
    }

    // 全局统计
    double total_avg_accept = 0.0;
    double total_avg_speedup = 0.0;

    for (const auto& r : results) {
        total_avg_accept += r.spec_accept_rate;
        total_avg_speedup += r.speedup;
    }

    total_avg_accept /= results.size();
    total_avg_speedup /= results.size();

    std::cout << "🌐 Overall Statistics:\n";
    std::cout << "   Total tests:            " << results.size() << "\n";
    std::cout << "   Average accept rate:    " << std::fixed << std::setprecision(2) << (total_avg_accept * 100) << "%\n";
    std::cout << "   Average speedup:        " << total_avg_speedup << "x\n";
    std::cout << "\n";
}

// 导出为 JSON
void export_to_json(const std::vector<BenchmarkResult>& results, const std::string& filename) {
    json j = json::array();

    for (const auto& r : results) {
        json entry = {
            {"scenario", r.scenario_name},
            {"prompt", r.prompt},
            {"normal", {
                {"time_ms", r.normal_time_ms},
                {"tokens", r.normal_tokens},
                {"tokens_per_sec", r.normal_tokens_per_sec},
                {"output", r.normal_output}
            }},
            {"speculative", {
                {"time_ms", r.spec_time_ms},
                {"tokens", r.spec_tokens},
                {"drafted", r.spec_drafted},
                {"accepted", r.spec_accepted},
                {"accept_rate", r.spec_accept_rate},
                {"tokens_per_sec", r.spec_tokens_per_sec},
                {"speedup", r.speedup},
                {"output", r.spec_output}
            }}
        };

        j.push_back(entry);
    }

    std::ofstream out(filename);
    out << std::setw(2) << j << std::endl;

    std::cout << "📄 Results exported to: " << filename << "\n";
}

// ============================================================
// Main
// ============================================================

int main(int argc, char** argv) {
    // 解析命令行参数
    std::string model_path;
    std::string draft_model_path;
    std::string output_file = "benchmark_results.json";
    int max_tokens = 100;
    float temperature = 0.7f;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        } else if (arg == "--model-draft" && i + 1 < argc) {
            draft_model_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--max-tokens" && i + 1 < argc) {
            max_tokens = std::stoi(argv[++i]);
        } else if (arg == "--temperature" && i + 1 < argc) {
            temperature = std::stof(argv[++i]);
        }
    }

    if (model_path.empty() || draft_model_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " --model PATH --model-draft PATH [--output FILE]\n";
        return 1;
    }

    // 初始化 llama.cpp
    llama_backend_init();

    // 获取 ModelManager
    auto& manager = ModelManager::instance();

    // 加载模型
    std::cout << "📦 Loading target model...\n";
    if (!manager.loadModel(model_path, 2048, 4)) {
        std::cerr << "❌ Failed to load target model\n";
        return 1;
    }

    std::cout << "📦 Loading draft model...\n";
    if (!manager.loadDraftModel(draft_model_path)) {
        std::cerr << "❌ Failed to load draft model\n";
        return 1;
    }

    // 获取测试场景
    auto scenarios = get_test_scenarios();

    // 运行基准测试
    auto results = run_benchmark(manager, scenarios, max_tokens, temperature);

    // 分析并打印结果
    analyze_and_print_results(results);

    // 导出 JSON
    export_to_json(results, output_file);

    // 清理
    llama_backend_free();

    return 0;
}
