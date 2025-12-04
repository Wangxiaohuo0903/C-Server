#include "TaskClassifier.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <iomanip>

// ============================================================
// 构造函数
// ============================================================

TaskClassifier::TaskClassifier(bool enable_verbose)
    : verbose_(enable_verbose)
{
    // 初始化所有任务类型的默认配置
    initializeDefaultConfigs();

    if (verbose_) {
        std::cout << "[TaskClassifier] Initialized with " << task_configs_.size()
                  << " task type configurations\n";
    }
}

// ============================================================
// 初始化默认配置
// ============================================================

void TaskClassifier::initializeDefaultConfigs() {
    // 1. 代码生成 - 高接受率，大draft
    task_configs_[TaskType::CODE_GENERATION] = {
        .n_draft = 24,                    // 较大draft数量
        .n_draft_min = 16,
        .n_draft_max = 32,
        .accept_rate_high = 0.70f,        // 代码生成接受率通常较高
        .accept_rate_low = 0.55f,
        .recommended_temperature = 0.3f,  // 代码生成倾向低温度
        .recommended_top_k = 50,
        .recommended_top_p = 0.95f,
        .description = "Code generation (high accept rate, large draft)"
    };

    // 2. 问答对话 - 中等接受率，中等draft
    task_configs_[TaskType::QA_CONVERSATION] = {
        .n_draft = 16,
        .n_draft_min = 12,
        .n_draft_max = 24,
        .accept_rate_high = 0.65f,
        .accept_rate_low = 0.45f,
        .recommended_temperature = 0.7f,
        .recommended_top_k = 40,
        .recommended_top_p = 0.9f,
        .description = "Q&A conversation (moderate accept rate)"
    };

    // 3. 创意写作 - 低接受率，小draft
    task_configs_[TaskType::CREATIVE_WRITING] = {
        .n_draft = 8,                     // 较小draft数量
        .n_draft_min = 4,
        .n_draft_max = 12,
        .accept_rate_high = 0.55f,        // 创意写作接受率较低
        .accept_rate_low = 0.35f,
        .recommended_temperature = 1.0f,  // 高温度
        .recommended_top_k = 50,
        .recommended_top_p = 0.95f,
        .description = "Creative writing (low accept rate, small draft)"
    };

    // 4. JSON/结构化输出 - 极高接受率，极大draft
    task_configs_[TaskType::JSON_GENERATION] = {
        .n_draft = 28,                    // 极大draft数量
        .n_draft_min = 20,
        .n_draft_max = 32,
        .accept_rate_high = 0.75f,        // JSON生成接受率极高
        .accept_rate_low = 0.60f,
        .recommended_temperature = 0.1f,  // 极低温度
        .recommended_top_k = 10,
        .recommended_top_p = 0.9f,
        .description = "JSON/structured output (very high accept rate)"
    };

    // 5. 翻译任务 - 高接受率，大draft
    task_configs_[TaskType::TRANSLATION] = {
        .n_draft = 20,
        .n_draft_min = 16,
        .n_draft_max = 28,
        .accept_rate_high = 0.68f,
        .accept_rate_low = 0.50f,
        .recommended_temperature = 0.5f,
        .recommended_top_k = 40,
        .recommended_top_p = 0.9f,
        .description = "Translation (high accept rate)"
    };

    // 6. 摘要生成 - 中高接受率
    task_configs_[TaskType::SUMMARIZATION] = {
        .n_draft = 18,
        .n_draft_min = 12,
        .n_draft_max = 24,
        .accept_rate_high = 0.65f,
        .accept_rate_low = 0.48f,
        .recommended_temperature = 0.5f,
        .recommended_top_k = 40,
        .recommended_top_p = 0.9f,
        .description = "Summarization (moderate-high accept rate)"
    };

    // 7. 数学推理 - 高接受率（步骤明确）
    task_configs_[TaskType::MATH_REASONING] = {
        .n_draft = 22,
        .n_draft_min = 16,
        .n_draft_max = 28,
        .accept_rate_high = 0.70f,
        .accept_rate_low = 0.52f,
        .recommended_temperature = 0.2f,  // 低温度保证准确性
        .recommended_top_k = 20,
        .recommended_top_p = 0.9f,
        .description = "Math reasoning (high accept rate, low temp)"
    };

    // 8. 通用任务 - 默认配置
    task_configs_[TaskType::GENERAL] = {
        .n_draft = 16,
        .n_draft_min = 8,
        .n_draft_max = 24,
        .accept_rate_high = 0.65f,
        .accept_rate_low = 0.45f,
        .recommended_temperature = 0.7f,
        .recommended_top_k = 40,
        .recommended_top_p = 0.9f,
        .description = "General task (default config)"
    };
}

// ============================================================
// 核心分类方法
// ============================================================

TaskClassifier::ClassificationResult TaskClassifier::classify(const std::string& prompt) {
    ClassificationResult result;
    result.confidence = 0.0f;

    // V1: 基于关键词的规则分类
    std::string reasoning;
    result.task_type = classifyByKeywords(prompt, reasoning);
    result.reasoning = reasoning;

    // 获取对应的配置
    result.config = getConfigForTaskType(result.task_type);

    // 置信度评估（V1简化版：基于关键词匹配）
    // TODO: V2可以基于多个特征计算置信度
    if (result.task_type != TaskType::GENERAL) {
        result.confidence = 0.8f;  // 匹配到关键词
    } else {
        result.confidence = 0.5f;  // 默认分类
    }

    if (verbose_) {
        std::cout << "[TaskClassifier] Classified as: " << taskTypeToString(result.task_type)
                  << " (confidence: " << (result.confidence * 100.0f) << "%)\n";
        std::cout << "[TaskClassifier] Reasoning: " << result.reasoning << "\n";
        std::cout << "[TaskClassifier] Recommended n_draft: " << result.config.n_draft << "\n";
    }

    return result;
}

// ============================================================
// 基于关键词的分类器 (V1)
// ============================================================

TaskType TaskClassifier::classifyByKeywords(
    const std::string& prompt,
    std::string& reasoning
) const {
    std::string prompt_lower = toLowerCase(prompt);

    // 1. 代码生成检测
    std::vector<std::string> code_keywords = {
        "python", "java", "c++", "javascript", "typescript", "golang", "rust",
        "代码", "code", "implement", "实现", "function", "函数", "class", "类",
        "algorithm", "算法", "debug", "调试", "write a program", "编写程序",
        "快速排序", "二分查找", "def ", "import ", "class ", "void ", "int main"
    };

    if (containsAnyKeyword(prompt_lower, code_keywords)) {
        reasoning = "Detected code-related keywords";
        return TaskType::CODE_GENERATION;
    }

    // 2. JSON/结构化输出检测
    std::vector<std::string> json_keywords = {
        "json", "xml", "yaml", "格式化", "structured", "结构化",
        "{", "}", "[", "]", "format the output", "生成json",
        "示例数据", "example data", "配置文件", "config file"
    };

    if (containsAnyKeyword(prompt_lower, json_keywords)) {
        reasoning = "Detected structured output keywords";
        return TaskType::JSON_GENERATION;
    }

    // 3. 数学推理检测
    std::vector<std::string> math_keywords = {
        "计算", "calculate", "solve", "求解", "数学", "math",
        "方程", "equation", "积分", "integral", "微分", "derivative",
        "证明", "prove", "推导", "deduce", "几何", "geometry",
        "+", "-", "*", "/", "=", "∫", "∑"
    };

    if (containsAnyKeyword(prompt_lower, math_keywords)) {
        reasoning = "Detected math-related keywords";
        return TaskType::MATH_REASONING;
    }

    // 4. 翻译检测
    std::vector<std::string> translation_keywords = {
        "翻译", "translate", "英译中", "中译英", "translate to",
        "用英语", "in english", "用中文", "in chinese",
        "français", "español", "deutsch"
    };

    if (containsAnyKeyword(prompt_lower, translation_keywords)) {
        reasoning = "Detected translation keywords";
        return TaskType::TRANSLATION;
    }

    // 5. 摘要生成检测
    std::vector<std::string> summary_keywords = {
        "摘要", "summary", "summarize", "总结", "概括",
        "简述", "briefly", "要点", "key points", "梗概", "overview"
    };

    if (containsAnyKeyword(prompt_lower, summary_keywords)) {
        reasoning = "Detected summarization keywords";
        return TaskType::SUMMARIZATION;
    }

    // 6. 创意写作检测
    std::vector<std::string> creative_keywords = {
        "写一首", "write a poem", "诗", "poem", "故事", "story",
        "小说", "novel", "散文", "essay", "创作", "creative writing",
        "想象", "imagine", "虚构", "fiction", "剧本", "script"
    };

    if (containsAnyKeyword(prompt_lower, creative_keywords)) {
        reasoning = "Detected creative writing keywords";
        return TaskType::CREATIVE_WRITING;
    }

    // 7. 问答对话检测（默认）
    std::vector<std::string> qa_keywords = {
        "什么", "什么是", "what", "what is", "why", "为什么",
        "how", "如何", "怎么", "解释", "explain", "describe", "描述",
        "告诉我", "tell me", "介绍", "introduce"
    };

    if (containsAnyKeyword(prompt_lower, qa_keywords)) {
        reasoning = "Detected Q&A keywords";
        return TaskType::QA_CONVERSATION;
    }

    // 8. 默认：通用任务
    reasoning = "No specific keywords matched, using general config";
    return TaskType::GENERAL;
}

// ============================================================
// 辅助方法
// ============================================================

bool TaskClassifier::containsAnyKeyword(
    const std::string& text,
    const std::vector<std::string>& keywords
) const {
    for (const auto& keyword : keywords) {
        if (text.find(keyword) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string TaskClassifier::toLowerCase(const std::string& text) const {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

TaskSpecificConfig TaskClassifier::getConfigForTaskType(TaskType type) const {
    auto it = task_configs_.find(type);
    if (it != task_configs_.end()) {
        return it->second;
    }

    // 如果找不到，返回通用配置
    return task_configs_.at(TaskType::GENERAL);
}

// ============================================================
// 在线学习与统计
// ============================================================

void TaskClassifier::recordFeedback(
    const std::string& prompt,
    TaskType task_type,
    double actual_accept_rate,
    double actual_speedup
) {
    std::lock_guard<std::mutex> g(stats_mutex_);

    auto& stats = task_stats_[task_type];
    stats.count++;
    stats.total_accept_rate += actual_accept_rate;
    stats.total_speedup += actual_speedup;

    if (verbose_) {
        std::cout << "[TaskClassifier] Feedback recorded for "
                  << taskTypeToString(task_type)
                  << ": accept_rate=" << (actual_accept_rate * 100.0) << "%"
                  << ", speedup=" << actual_speedup << "x\n";
    }
}

void TaskClassifier::printStatistics() const {
    std::lock_guard<std::mutex> g(stats_mutex_);

    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Task Classifier Statistics                      ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n\n";

    std::cout << std::left << std::setw(20) << "Task Type"
              << std::right << std::setw(10) << "Count"
              << std::setw(15) << "Avg Accept"
              << std::setw(15) << "Avg Speedup" << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (const auto& [type, stats] : task_stats_) {
        if (stats.count > 0) {
            std::cout << std::left << std::setw(20) << taskTypeToString(type)
                      << std::right << std::setw(10) << stats.count
                      << std::setw(14) << std::fixed << std::setprecision(1)
                      << (stats.avg_accept_rate() * 100.0) << "%"
                      << std::setw(14) << std::setprecision(2)
                      << stats.avg_speedup() << "x\n";
        }
    }

    std::cout << "\n";
}

void TaskClassifier::resetStatistics() {
    std::lock_guard<std::mutex> g(stats_mutex_);
    task_stats_.clear();

    if (verbose_) {
        std::cout << "[TaskClassifier] Statistics reset\n";
    }
}

void TaskClassifier::setCustomConfig(TaskType type, const TaskSpecificConfig& config) {
    task_configs_[type] = config;

    if (verbose_) {
        std::cout << "[TaskClassifier] Custom config set for "
                  << taskTypeToString(type) << "\n";
    }
}
