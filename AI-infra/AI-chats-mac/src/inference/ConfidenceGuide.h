/**
 * @file ConfidenceGuide.h
 * @brief Token级置信度引导实现
 *
 * 实现基于Shannon熵的Token置信度计算和自适应推测策略
 *
 * @author AI-infra Team
 * @date 2025-11-27
 */

#ifndef CONFIDENCE_GUIDE_H
#define CONFIDENCE_GUIDE_H

#include <vector>
#include <string>

//=============================================================================
// TokenConfidenceCalculator - Token置信度计算器
//=============================================================================

/**
 * @brief 基于Shannon熵的Token置信度计算
 */
class TokenConfidenceCalculator {
public:
    static constexpr float EPSILON = 1e-10f;

    /**
     * 计算token的置信度
     * @param logits 模型输出的logits分布
     * @return 置信度值 [0, 1]，越高表示模型越确定
     */
    static float calculate(const std::vector<float>& logits);

    /**
     * Softmax归一化
     * @param logits 输入logits
     * @return 归一化后的概率分布
     */
    static std::vector<float> softmax(const std::vector<float>& logits);

    /**
     * 计算Shannon熵
     * @param probs 概率分布
     * @return 熵值
     */
    static float calculateEntropy(const std::vector<float>& probs);

    /**
     * 计算Top-1概率
     * @param logits 模型输出的logits
     * @return 最大概率值
     */
    static float calculateTop1Probability(const std::vector<float>& logits);

    /**
     * 计算Top-K多样性
     * @param logits 模型输出的logits
     * @param k Top-K大小
     * @return Top-K的熵归一化值
     */
    static float calculateTopKDiversity(const std::vector<float>& logits, int k = 5);
};

//=============================================================================
// ConfidenceGuidedStrategy - 置信度引导的自适应推测策略
//=============================================================================

/**
 * @brief 根据置信度动态调整推测窗口大小
 */
class ConfidenceGuidedStrategy {
public:
    struct Config {
        float high_threshold = 0.85f;      // 高置信度阈值
        float low_threshold = 0.65f;       // 低置信度阈值
        int min_n_draft = 4;               // 最小推测窗口
        int max_n_draft = 32;              // 最大推测窗口
        float increase_factor = 1.5f;      // 增长因子
        float decrease_factor = 0.7f;      // 减少因子
        bool enable_verbose = false;       // 详细日志
    };

    struct Statistics {
        int total_adjustments = 0;         // 总调整次数
        int increased_count = 0;           // 增加次数
        int decreased_count = 0;           // 减少次数
        std::vector<float> confidence_history;  // 置信度历史
    };

    ConfidenceGuidedStrategy(const Config& config);
    ConfidenceGuidedStrategy();  // 默认构造函数

    /**
     * 根据置信度调整n_draft (三档策略)
     * @param confidence 当前token的置信度
     * @param current_n_draft 当前的n_draft值
     * @return 调整后的n_draft
     */
    int adjustDraftSize(float confidence, int current_n_draft);

    /**
     * 根据置信度调整n_draft (平滑策略)
     * @param confidence 当前token的置信度
     * @param current_n_draft 当前的n_draft值
     * @return 调整后的n_draft
     */
    int adjustDraftSizeSmooth(float confidence, int current_n_draft);

    /**
     * 获取统计信息
     */
    Statistics getStatistics() const;

    /**
     * 重置统计数据
     */
    void reset();

    /**
     * 获取平均置信度
     */
    float getAverageConfidence() const;

    /**
     * 获取最小置信度
     */
    float getMinConfidence() const;

    /**
     * 获取最大置信度
     */
    float getMaxConfidence() const;

    /**
     * 打印统计信息
     */
    void printStatistics() const;

private:
    Config config_;
    Statistics stats_;
};

//=============================================================================
// ConfidenceAnalyzer - 置信度与Accept Rate相关性分析
//=============================================================================

/**
 * @brief 分析置信度与Accept Rate的相关性
 *
 * 用于实验验证置信度指标的有效性
 */
class ConfidenceAnalyzer {
public:
    struct ConfidenceSample {
        float confidence;
        bool accepted;
    };

    ConfidenceAnalyzer();

    /**
     * 记录一个样本
     * @param confidence token置信度
     * @param accepted 是否被accept
     */
    void recordSample(float confidence, bool accepted);

    /**
     * 计算Pearson相关系数
     */
    float calculateCorrelation() const;

    /**
     * 打印分bin统计
     */
    void printBinStatistics() const;

    /**
     * 导出CSV数据
     */
    void saveToCSV(const std::string& filename) const;

    /**
     * 获取所有样本
     */
    std::vector<ConfidenceSample> getSamples() const;

    /**
     * 清空数据
     */
    void clear();

private:
    std::vector<ConfidenceSample> samples_;
    std::vector<float> bins_;
    std::vector<int> bin_counts_;
    std::vector<int> bin_accept_counts_;
};

#endif // CONFIDENCE_GUIDE_H
