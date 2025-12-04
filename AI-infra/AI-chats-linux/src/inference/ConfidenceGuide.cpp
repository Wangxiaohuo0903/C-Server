/**
 * @file ConfidenceGuide.cpp
 * @brief Token级置信度引导实现
 *
 * 实现基于Shannon熵的Token置信度计算和自适应推测策略
 *
 * @author AI-infra Team
 * @date 2025-11-27
 */

#include "ConfidenceGuide.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <fstream>

//=============================================================================
// TokenConfidenceCalculator 实现
//=============================================================================

float TokenConfidenceCalculator::calculate(const std::vector<float>& logits) {
    if (logits.empty()) {
        return 0.0f;
    }

    // 1. Softmax归一化
    std::vector<float> probs = softmax(logits);

    // 2. 计算Shannon熵
    float entropy = calculateEntropy(probs);

    // 3. 归一化置信度
    // max_entropy = log2(vocab_size)
    float max_entropy = std::log2(static_cast<float>(logits.size()));

    // confidence = 1 - (H / H_max)
    // 熵越低，置信度越高
    float confidence = 1.0f - (entropy / max_entropy);

    // 4. 限制在[0, 1]范围内
    return std::max(0.0f, std::min(1.0f, confidence));
}

std::vector<float> TokenConfidenceCalculator::softmax(
    const std::vector<float>& logits
) {
    if (logits.empty()) {
        return {};
    }

    // 1. 找到最大值，避免数值溢出
    float max_logit = *std::max_element(logits.begin(), logits.end());

    // 2. 计算exp(x - max)
    std::vector<float> exp_values(logits.size());
    float sum_exp = 0.0f;

    for (size_t i = 0; i < logits.size(); ++i) {
        exp_values[i] = std::exp(logits[i] - max_logit);
        sum_exp += exp_values[i];
    }

    // 3. 归一化
    std::vector<float> probs(logits.size());
    for (size_t i = 0; i < logits.size(); ++i) {
        probs[i] = exp_values[i] / sum_exp;
    }

    return probs;
}

float TokenConfidenceCalculator::calculateEntropy(
    const std::vector<float>& probs
) {
    float entropy = 0.0f;

    for (float p : probs) {
        if (p > EPSILON) {  // 避免log(0)
            entropy -= p * std::log2(p);
        }
    }

    return entropy;
}

float TokenConfidenceCalculator::calculateTop1Probability(
    const std::vector<float>& logits
) {
    if (logits.empty()) {
        return 0.0f;
    }

    std::vector<float> probs = softmax(logits);
    return *std::max_element(probs.begin(), probs.end());
}

float TokenConfidenceCalculator::calculateTopKDiversity(
    const std::vector<float>& logits,
    int k
) {
    if (logits.empty() || k <= 0) {
        return 0.0f;
    }

    std::vector<float> probs = softmax(logits);

    // 获取Top-K概率
    std::vector<float> sorted_probs = probs;
    std::partial_sort(sorted_probs.begin(),
                     sorted_probs.begin() + std::min(k, static_cast<int>(sorted_probs.size())),
                     sorted_probs.end(),
                     std::greater<float>());

    // 计算Top-K的熵
    float top_k_entropy = 0.0f;
    for (int i = 0; i < std::min(k, static_cast<int>(sorted_probs.size())); ++i) {
        float p = sorted_probs[i];
        if (p > EPSILON) {
            top_k_entropy -= p * std::log2(p);
        }
    }

    // 归一化到[0, 1]
    float max_top_k_entropy = std::log2(static_cast<float>(std::min(k, static_cast<int>(probs.size()))));
    return (max_top_k_entropy > 0) ? (top_k_entropy / max_top_k_entropy) : 0.0f;
}

//=============================================================================
// ConfidenceGuidedStrategy 实现
//=============================================================================

ConfidenceGuidedStrategy::ConfidenceGuidedStrategy(const Config& config)
    : config_(config) {
    // 初始化统计数据
    stats_.total_adjustments = 0;
    stats_.increased_count = 0;
    stats_.decreased_count = 0;
    stats_.confidence_history.reserve(1000);
}

ConfidenceGuidedStrategy::ConfidenceGuidedStrategy()
    : config_(Config()) {
    // 初始化统计数据
    stats_.total_adjustments = 0;
    stats_.increased_count = 0;
    stats_.decreased_count = 0;
    stats_.confidence_history.reserve(1000);
}

int ConfidenceGuidedStrategy::adjustDraftSize(
    float confidence,
    int current_n_draft
) {
    // 记录置信度历史
    stats_.confidence_history.push_back(confidence);

    int new_n_draft = current_n_draft;

    // 根据置信度调整n_draft
    if (confidence >= config_.high_threshold) {
        // 高置信度: 增加推测窗口 (aggressive)
        new_n_draft = static_cast<int>(current_n_draft * config_.increase_factor);
        new_n_draft = std::min(new_n_draft, config_.max_n_draft);

        if (new_n_draft > current_n_draft) {
            stats_.increased_count++;
            stats_.total_adjustments++;

            if (config_.enable_verbose) {
                std::cout << "[ConfidenceGuide] High confidence ("
                         << std::fixed << std::setprecision(3) << confidence
                         << "): Increase n_draft " << current_n_draft
                         << " → " << new_n_draft << std::endl;
            }
        }
    }
    else if (confidence < config_.low_threshold) {
        // 低置信度: 减少推测窗口 (conservative)
        new_n_draft = static_cast<int>(current_n_draft * config_.decrease_factor);
        new_n_draft = std::max(new_n_draft, config_.min_n_draft);

        if (new_n_draft < current_n_draft) {
            stats_.decreased_count++;
            stats_.total_adjustments++;

            if (config_.enable_verbose) {
                std::cout << "[ConfidenceGuide] Low confidence ("
                         << std::fixed << std::setprecision(3) << confidence
                         << "): Decrease n_draft " << current_n_draft
                         << " → " << new_n_draft << std::endl;
            }
        }
    }
    else {
        // 中等置信度: 保持不变
        if (config_.enable_verbose && stats_.confidence_history.size() % 10 == 0) {
            std::cout << "[ConfidenceGuide] Moderate confidence ("
                     << std::fixed << std::setprecision(3) << confidence
                     << "): Keep n_draft = " << current_n_draft << std::endl;
        }
    }

    return new_n_draft;
}

int ConfidenceGuidedStrategy::adjustDraftSizeSmooth(
    float confidence,
    int current_n_draft
) {
    // 平滑调整策略: 线性插值
    // confidence ∈ [0, 1] → multiplier ∈ [decrease_factor, increase_factor]

    float multiplier;
    if (confidence >= config_.high_threshold) {
        multiplier = config_.increase_factor;
    } else if (confidence <= config_.low_threshold) {
        multiplier = config_.decrease_factor;
    } else {
        // 线性插值
        float t = (confidence - config_.low_threshold) /
                 (config_.high_threshold - config_.low_threshold);
        multiplier = config_.decrease_factor +
                    t * (config_.increase_factor - config_.decrease_factor);
    }

    int new_n_draft = static_cast<int>(current_n_draft * multiplier);
    new_n_draft = std::max(config_.min_n_draft,
                           std::min(config_.max_n_draft, new_n_draft));

    // 统计
    if (new_n_draft > current_n_draft) {
        stats_.increased_count++;
        stats_.total_adjustments++;
    } else if (new_n_draft < current_n_draft) {
        stats_.decreased_count++;
        stats_.total_adjustments++;
    }

    stats_.confidence_history.push_back(confidence);

    return new_n_draft;
}

ConfidenceGuidedStrategy::Statistics ConfidenceGuidedStrategy::getStatistics() const {
    return stats_;
}

void ConfidenceGuidedStrategy::reset() {
    stats_.total_adjustments = 0;
    stats_.increased_count = 0;
    stats_.decreased_count = 0;
    stats_.confidence_history.clear();
}

float ConfidenceGuidedStrategy::getAverageConfidence() const {
    if (stats_.confidence_history.empty()) {
        return 0.0f;
    }

    float sum = std::accumulate(stats_.confidence_history.begin(),
                               stats_.confidence_history.end(),
                               0.0f);
    return sum / stats_.confidence_history.size();
}

float ConfidenceGuidedStrategy::getMinConfidence() const {
    if (stats_.confidence_history.empty()) {
        return 0.0f;
    }

    return *std::min_element(stats_.confidence_history.begin(),
                            stats_.confidence_history.end());
}

float ConfidenceGuidedStrategy::getMaxConfidence() const {
    if (stats_.confidence_history.empty()) {
        return 0.0f;
    }

    return *std::max_element(stats_.confidence_history.begin(),
                            stats_.confidence_history.end());
}

void ConfidenceGuidedStrategy::printStatistics() const {
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║         Confidence-Guided Strategy Statistics           ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Total adjustments:    " << stats_.total_adjustments << "\n";
    std::cout << "  - Increased:        " << stats_.increased_count << "\n";
    std::cout << "  - Decreased:        " << stats_.decreased_count << "\n";
    std::cout << "  - Kept same:        "
              << (stats_.confidence_history.size() - stats_.total_adjustments) << "\n";

    if (!stats_.confidence_history.empty()) {
        std::cout << "\nConfidence Statistics:\n";
        std::cout << "  - Average:          " << getAverageConfidence() << "\n";
        std::cout << "  - Min:              " << getMinConfidence() << "\n";
        std::cout << "  - Max:              " << getMaxConfidence() << "\n";
        std::cout << "  - Samples:          " << stats_.confidence_history.size() << "\n";
    }

    std::cout << "\nThreshold Configuration:\n";
    std::cout << "  - High threshold:   " << config_.high_threshold << "\n";
    std::cout << "  - Low threshold:    " << config_.low_threshold << "\n";
    std::cout << "  - n_draft range:    [" << config_.min_n_draft
              << ", " << config_.max_n_draft << "]\n";
    std::cout << std::endl;
}

//=============================================================================
// ConfidenceAnalyzer 实现
//=============================================================================

ConfidenceAnalyzer::ConfidenceAnalyzer() {
    bins_ = {0.0f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};
    bin_counts_.resize(bins_.size() - 1, 0);
    bin_accept_counts_.resize(bins_.size() - 1, 0);
}

void ConfidenceAnalyzer::recordSample(float confidence, bool accepted) {
    samples_.push_back({confidence, accepted});

    // 更新bin统计
    for (size_t i = 0; i < bins_.size() - 1; ++i) {
        if (confidence >= bins_[i] && confidence < bins_[i + 1]) {
            bin_counts_[i]++;
            if (accepted) {
                bin_accept_counts_[i]++;
            }
            break;
        }
    }
}

float ConfidenceAnalyzer::calculateCorrelation() const {
    if (samples_.size() < 2) {
        return 0.0f;
    }

    // 计算Pearson相关系数
    // r = Σ[(x - x̄)(y - ȳ)] / √[Σ(x - x̄)² × Σ(y - ȳ)²]

    size_t n = samples_.size();

    // 计算均值
    float mean_confidence = 0.0f;
    float mean_accepted = 0.0f;
    for (const auto& sample : samples_) {
        mean_confidence += sample.confidence;
        mean_accepted += (sample.accepted ? 1.0f : 0.0f);
    }
    mean_confidence /= n;
    mean_accepted /= n;

    // 计算协方差和方差
    float covariance = 0.0f;
    float var_confidence = 0.0f;
    float var_accepted = 0.0f;

    for (const auto& sample : samples_) {
        float dx = sample.confidence - mean_confidence;
        float dy = (sample.accepted ? 1.0f : 0.0f) - mean_accepted;

        covariance += dx * dy;
        var_confidence += dx * dx;
        var_accepted += dy * dy;
    }

    // 计算相关系数
    float denominator = std::sqrt(var_confidence * var_accepted);
    if (denominator < 1e-10) {
        return 0.0f;
    }

    return covariance / denominator;
}

void ConfidenceAnalyzer::printBinStatistics() const {
    std::cout << "\n╔══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     Confidence vs Accept Rate Analysis (By Bins)        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════╝\n";

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "\nConfidence Range    Accept Rate    Sample Count\n";
    std::cout << "────────────────────────────────────────────────────────\n";

    for (size_t i = 0; i < bins_.size() - 1; ++i) {
        std::cout << "[" << std::setw(4) << (bins_[i] * 100) << "%, "
                  << std::setw(4) << (bins_[i + 1] * 100) << "%)";

        if (bin_counts_[i] > 0) {
            float accept_rate = static_cast<float>(bin_accept_counts_[i]) / bin_counts_[i];
            std::cout << "        " << std::setw(5) << std::setprecision(1)
                      << (accept_rate * 100) << "%";
        } else {
            std::cout << "          N/A";
        }

        std::cout << "           " << std::setw(4) << bin_counts_[i] << "\n";
    }

    std::cout << "\nPearson Correlation: r = "
              << std::setprecision(3) << calculateCorrelation() << "\n";
    std::cout << "Total Samples:       " << samples_.size() << "\n";
    std::cout << std::endl;
}

void ConfidenceAnalyzer::saveToCSV(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }

    // 写入CSV头
    file << "confidence,accepted\n";

    // 写入数据
    for (const auto& sample : samples_) {
        file << sample.confidence << ","
             << (sample.accepted ? "1" : "0") << "\n";
    }

    file.close();
    std::cout << "Saved " << samples_.size()
              << " samples to " << filename << std::endl;
}

std::vector<ConfidenceAnalyzer::ConfidenceSample>
ConfidenceAnalyzer::getSamples() const {
    return samples_;
}

void ConfidenceAnalyzer::clear() {
    samples_.clear();
    std::fill(bin_counts_.begin(), bin_counts_.end(), 0);
    std::fill(bin_accept_counts_.begin(), bin_accept_counts_.end(), 0);
}
