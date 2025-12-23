#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

/**
 * @file TaskClassifier.h
 * @brief 任务类型分类器 - 用于识别prompt的任务类型并推荐最优配置
 *
 * 核心思想：
 * - 不同任务类型（代码、问答、创意等）的推测式解码特性不同
 * - 通过识别任务类型，自动应用针对性优化配置
 * - 提升5-20%性能（相比通用配置）
 *
 * 版本：
 * - V1: 基于关键词的规则分类器（当前实现）
 * - V2: 基于TF-IDF的统计分类器（未来）
 * - V3: 基于ML的智能分类器（未来）
 */

// ============================================================
// 任务类型定义
// ============================================================

/**
 * @brief 任务类型枚举
 */
enum class TaskType {
    CODE_GENERATION,    // 代码生成（Python, C++, JavaScript等）
    QA_CONVERSATION,    // 问答对话（一般性问答）
    CREATIVE_WRITING,   // 创意写作（诗歌、故事、文章）
    JSON_GENERATION,    // 结构化输出（JSON, XML, YAML等）
    TRANSLATION,        // 翻译任务
    SUMMARIZATION,      // 摘要生成
    MATH_REASONING,     // 数学推理
    GENERAL,            // 通用任务（无法分类时的默认值）
};

/**
 * @brief 将任务类型转换为字符串
 */
inline std::string taskTypeToString(TaskType type) {
    switch (type) {
        case TaskType::CODE_GENERATION:  return "CODE_GENERATION";
        case TaskType::QA_CONVERSATION:  return "QA_CONVERSATION";
        case TaskType::CREATIVE_WRITING: return "CREATIVE_WRITING";
        case TaskType::JSON_GENERATION:  return "JSON_GENERATION";
        case TaskType::TRANSLATION:      return "TRANSLATION";
        case TaskType::SUMMARIZATION:    return "SUMMARIZATION";
        case TaskType::MATH_REASONING:   return "MATH_REASONING";
        case TaskType::GENERAL:          return "GENERAL";
        default:                         return "UNKNOWN";
    }
}

// ============================================================
// 任务特定配置
// ============================================================

/**
 * @brief 针对特定任务类型的推测式解码配置
 */
struct TaskSpecificConfig {
    // Draft参数
    int n_draft = 16;           // Draft token数量
    int n_draft_min = 8;        // 最小draft数量
    int n_draft_max = 32;       // 最大draft数量

    // 自适应参数
    float accept_rate_high = 0.65f;  // 高接受率阈值
    float accept_rate_low = 0.45f;   // 低接受率阈值

    // 采样参数建议
    float recommended_temperature = 0.7f;  // 推荐温度
    int recommended_top_k = 40;            // 推荐top_k
    float recommended_top_p = 0.9f;        // 推荐top_p

    // 特征描述
    std::string description;    // 任务特征描述
};

// ============================================================
// 任务分类器
// ============================================================

/**
 * @brief 任务类型分类器
 *
 * 功能：
 * 1. 根据prompt文本识别任务类型
 * 2. 返回针对该任务类型的最优配置
 * 3. 支持在线学习（记录实际效果，优化分类）
 *
 * 使用示例：
 * ```cpp
 * TaskClassifier classifier;
 * auto result = classifier.classify("用Python实现快速排序");
 * // result.task_type == TaskType::CODE_GENERATION
 * // result.config.n_draft == 24
 * ```
 */
class TaskClassifier {
public:
    // ============ 分类结果 ============

    struct ClassificationResult {
        TaskType task_type;              // 识别出的任务类型
        TaskSpecificConfig config;       // 推荐的配置
        float confidence;                // 分类置信度 (0.0-1.0)
        std::string reasoning;           // 分类依据（用于调试）
    };

    // ============ 构造与配置 ============

    /**
     * @brief 构造函数
     * @param enable_verbose 是否打印详细日志
     */
    explicit TaskClassifier(bool enable_verbose = false);

    ~TaskClassifier() = default;

    // ============ 核心功能 ============

    /**
     * @brief 分类prompt并返回推荐配置
     * @param prompt 用户输入的prompt
     * @return 分类结果（包含任务类型和推荐配置）
     */
    ClassificationResult classify(const std::string& prompt);

    /**
     * @brief 获取指定任务类型的默认配置
     * @param type 任务类型
     * @return 该任务类型的最优配置
     */
    TaskSpecificConfig getConfigForTaskType(TaskType type) const;

    // ============ 在线学习（可选） ============

    /**
     * @brief 记录实际推理效果，用于优化分类器
     * @param prompt 输入prompt
     * @param task_type 分类的任务类型
     * @param actual_accept_rate 实际接受率
     * @param actual_speedup 实际加速比
     */
    void recordFeedback(
        const std::string& prompt,
        TaskType task_type,
        double actual_accept_rate,
        double actual_speedup
    );

    /**
     * @brief 打印统计信息（每种任务类型的平均性能）
     */
    void printStatistics() const;

    /**
     * @brief 重置统计信息
     */
    void resetStatistics();

    // ============ 配置管理 ============

    /**
     * @brief 设置是否启用详细日志
     */
    void setVerbose(bool enable) { verbose_ = enable; }

    /**
     * @brief 自定义某个任务类型的配置
     * @param type 任务类型
     * @param config 自定义配置
     */
    void setCustomConfig(TaskType type, const TaskSpecificConfig& config);

private:
    // ============ 内部状态 ============

    bool verbose_;  // 是否打印详细日志

    // 任务类型 → 最优配置的映射
    std::unordered_map<TaskType, TaskSpecificConfig> task_configs_;

    // 统计信息（用于在线学习）
    struct TaskStats {
        uint64_t count = 0;              // 分类次数
        double total_accept_rate = 0.0;  // 累计接受率
        double total_speedup = 0.0;      // 累计加速比

        double avg_accept_rate() const {
            return count > 0 ? total_accept_rate / count : 0.0;
        }

        double avg_speedup() const {
            return count > 0 ? total_speedup / count : 0.0;
        }
    };

    mutable std::mutex stats_mutex_;
    std::unordered_map<TaskType, TaskStats> task_stats_;

    // ============ 内部方法 ============

    /**
     * @brief 初始化所有任务类型的默认配置
     */
    void initializeDefaultConfigs();

    /**
     * @brief V1 分类器：基于关键词规则
     */
    TaskType classifyByKeywords(const std::string& prompt, std::string& reasoning) const;

    /**
     * @brief 检查文本中是否包含关键词列表中的任意一个
     */
    bool containsAnyKeyword(
        const std::string& text,
        const std::vector<std::string>& keywords
    ) const;

    /**
     * @brief 转换为小写（用于不区分大小写的匹配）
     */
    std::string toLowerCase(const std::string& text) const;
};

/**
 * @brief 创建任务分类器的工厂函数
 */
inline std::unique_ptr<TaskClassifier> createTaskClassifier(bool verbose = false) {
    return std::make_unique<TaskClassifier>(verbose);
}
