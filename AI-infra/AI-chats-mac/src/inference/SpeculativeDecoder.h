#pragma once

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

// Forward declarations for llama.cpp types
struct llama_model;
struct llama_context;
struct llama_batch;

// ============================================================
// 推测式解码配置
// ============================================================
struct SpeculativeConfig {
    // 小模型路径 (drafter)
    std::string draft_model_path;

    // 推测式解码参数
    int draft_tokens_K = 4;         // 每次起草的token数量 (论文推荐4-8)
    bool enable_spec_decode = true; // 是否启用推测式解码

    // Drafter模型配置
    int draft_n_ctx = 512;          // 小模型上下文长度 (较短即可)
    int draft_n_threads = 2;        // CPU推理线程数
    int draft_n_gpu_layers = 0;     // GPU层数 (0=纯CPU, drafter通常用CPU)

    // 校验参数
    float rejection_threshold = 0.1; // 概率差异阈值 (越小越严格)
    bool use_probability_matching = true; // 是否使用概率匹配校验

    // 动态调整
    bool enable_dynamic_K = false;   // 是否根据接受率动态调整K
    int min_K = 2;                   // 动态K的下限
    int max_K = 8;                   // 动态K的上限
    float target_acceptance_rate = 0.65f; // 目标接受率

    // 构造函数：提供默认配置
    SpeculativeConfig() = default;

    // 工厂方法：根据硬件平台创建配置
    static SpeculativeConfig createForAppleSilicon() {
        SpeculativeConfig cfg;
        cfg.draft_n_threads = 4;        // Apple Silicon性能核心
        cfg.draft_n_gpu_layers = 23;    // Drafter全部offload到GPU (10-20x加速!)
        cfg.draft_tokens_K = 5;         // M1/M2/M3有强大Metal GPU
        return cfg;
    }

    static SpeculativeConfig createForCPUOnly() {
        SpeculativeConfig cfg;
        cfg.draft_n_threads = 2;
        cfg.draft_n_gpu_layers = 0;
        cfg.draft_tokens_K = 3;         // 纯CPU场景保守起草
        return cfg;
    }
};

// ============================================================
// 推测式解码统计信息
// ============================================================
struct SpeculativeStats {
    // 基础统计
    std::atomic<uint64_t> total_drafted{0};     // 总起草token数
    std::atomic<uint64_t> total_accepted{0};    // 总接受token数
    std::atomic<uint64_t> total_steps{0};       // 总解码步数

    // 时间统计 (纳秒)
    std::atomic<uint64_t> total_draft_time_ns{0};   // 起草阶段总耗时
    std::atomic<uint64_t> total_verify_time_ns{0};  // 校验阶段总耗时

    // 动态K值追踪
    std::atomic<int> current_K{4};              // 当前K值
    std::atomic<uint32_t> consecutive_low_accept{0}; // 连续低接受率次数
    std::atomic<uint32_t> consecutive_high_accept{0}; // 连续高接受率次数

    // 滑动窗口接受率追踪 (最近N步)
    static constexpr size_t WINDOW_SIZE = 10;
    std::array<float, WINDOW_SIZE> recent_acceptance_rates{0.0f};
    std::atomic<size_t> window_index{0};
    std::mutex window_mutex;  // 保护滑动窗口更新

    // 计算接受率 (线程安全读取)
    float getAcceptanceRate() const {
        uint64_t drafted = total_drafted.load(std::memory_order_relaxed);
        uint64_t accepted = total_accepted.load(std::memory_order_relaxed);
        return drafted > 0 ? static_cast<float>(accepted) / drafted : 0.0f;
    }

    // 计算平均加速比 (相比贪婪解码)
    float getSpeedup() const {
        uint64_t steps = total_steps.load(std::memory_order_relaxed);
        uint64_t accepted = total_accepted.load(std::memory_order_relaxed);
        // 贪婪解码: steps步生成steps个token
        // 推测式解码: steps步生成accepted个token
        return steps > 0 ? static_cast<float>(accepted) / steps : 1.0f;
    }

    // 计算平均起草时间 (毫秒)
    double getAvgDraftTimeMs() const {
        uint64_t steps = total_steps.load(std::memory_order_relaxed);
        uint64_t time_ns = total_draft_time_ns.load(std::memory_order_relaxed);
        return steps > 0 ? (time_ns / 1e6) / steps : 0.0;
    }

    // 计算平均校验时间 (毫秒)
    double getAvgVerifyTimeMs() const {
        uint64_t steps = total_steps.load(std::memory_order_relaxed);
        uint64_t time_ns = total_verify_time_ns.load(std::memory_order_relaxed);
        return steps > 0 ? (time_ns / 1e6) / steps : 0.0;
    }

    // 更新滑动窗口接受率
    void updateRecentAcceptanceRate(float step_acceptance_rate) {
        std::lock_guard<std::mutex> lock(window_mutex);
        size_t idx = window_index.fetch_add(1, std::memory_order_relaxed) % WINDOW_SIZE;
        recent_acceptance_rates[idx] = step_acceptance_rate;
    }

    // 计算近期平均接受率 (滑动窗口)
    float getRecentAcceptanceRate() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(window_mutex));
        size_t total_entries = std::min(window_index.load(std::memory_order_relaxed), WINDOW_SIZE);
        if (total_entries == 0) return 0.0f;

        float sum = 0.0f;
        for (size_t i = 0; i < total_entries; ++i) {
            sum += recent_acceptance_rates[i];
        }
        return sum / total_entries;
    }

    // 重置统计
    void reset() {
        total_drafted.store(0, std::memory_order_relaxed);
        total_accepted.store(0, std::memory_order_relaxed);
        total_steps.store(0, std::memory_order_relaxed);
        total_draft_time_ns.store(0, std::memory_order_relaxed);
        total_verify_time_ns.store(0, std::memory_order_relaxed);
        consecutive_low_accept.store(0, std::memory_order_relaxed);
        consecutive_high_accept.store(0, std::memory_order_relaxed);
        window_index.store(0, std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(window_mutex));
        recent_acceptance_rates.fill(0.0f);
    }
};

// ============================================================
// 推测式解码器核心类
// ============================================================
class SpeculativeDecoder {
public:
    // 构造函数：传入配置
    explicit SpeculativeDecoder(const SpeculativeConfig& config);

    // 析构函数：清理资源
    ~SpeculativeDecoder();

    // 禁止拷贝
    SpeculativeDecoder(const SpeculativeDecoder&) = delete;
    SpeculativeDecoder& operator=(const SpeculativeDecoder&) = delete;

    // ============ 初始化接口 ============

    // 加载drafter模型
    // 返回: 成功返回true, 失败返回false
    bool loadDraftModel();

    // 卸载drafter模型 (释放内存)
    void unloadDraftModel();

    // 检查是否已加载
    bool isLoaded() const { return draft_model_ != nullptr && draft_ctx_ != nullptr; }

    // ============ 配置验证 ============

    // 验证Drafter与Verifier的兼容性
    // verifier_model: 大模型句柄
    // 返回: 兼容性检查结果
    struct CompatibilityResult {
        bool is_compatible = true;
        std::string error_message;

        // 具体检查项
        bool vocab_match = false;       // 词表是否匹配
        bool tokenizer_match = false;   // 分词器是否匹配
        int vocab_size_draft = 0;
        int vocab_size_verifier = 0;

        std::string getSummary() const;
    };
    CompatibilityResult checkCompatibility(llama_model* verifier_model) const;

    // 智能判断是否应启用推测式解码
    // expected_tokens: 预期生成token数
    // 返回: true=建议启用, false=建议禁用
    bool shouldEnableForTask(int expected_tokens) const {
        // 极短回答(<10 tokens)不建议使用
        const int MIN_TOKENS_THRESHOLD = 10;
        return expected_tokens >= MIN_TOKENS_THRESHOLD;
    }

    // ============ 核心解码接口 ============

    // 推测式解码生成tokens
    // verifier_ctx: 大模型的llama_context (verifier)
    // prompt_tokens: 已经tokenize的prompt
    // max_tokens: 最大生成token数
    // temperature: 采样温度
    // stop_token: 停止token (通常是EOS)
    // 返回: 生成的token序列
    std::vector<int> decode(
        llama_context* verifier_ctx,
        const std::vector<int>& prompt_tokens,
        int max_tokens,
        float temperature,
        int stop_token
    );

    // ============ 统计接口 ============

    // 获取统计信息
    const SpeculativeStats& getStats() const { return stats_; }

    // 重置统计
    void resetStats() { stats_.reset(); }

    // 打印统计信息 (用于调试/日志)
    std::string getStatsString() const;

    // ============ 配置管理 ============

    // 获取当前配置
    const SpeculativeConfig& getConfig() const { return config_; }

    // 更新配置 (部分参数可运行时调整)
    void updateConfig(const SpeculativeConfig& new_config);

private:
    // ============ 内部方法 ============

    // 起草K个tokens (使用小模型)
    // input_tokens: 输入token序列 (包括prompt + 已生成的tokens)
    // K: 要起草的token数量
    // temperature: 采样温度
    // 返回: 起草的token序列
    std::vector<int> draftTokens(
        const std::vector<int>& input_tokens,
        int K,
        float temperature
    );

    // 批量校验tokens (使用大模型)
    // verifier_ctx: 大模型context
    // input_tokens: 输入序列
    // draft_tokens: 小模型起草的tokens
    // temperature: 采样温度
    // 返回: 接受的token数量 (0-K, 最多K+1个因为verifier会额外生成1个)
    struct VerifyResult {
        int accepted_count;              // 接受的draft token数量
        std::vector<int> accepted_tokens; // 实际接受的tokens (可能包括verifier的额外token)
    };
    VerifyResult verifyTokensBatch(
        llama_context* verifier_ctx,
        const std::vector<int>& input_tokens,
        const std::vector<int>& draft_tokens,
        float temperature
    );

    // 动态调整K值 (根据接受率)
    void adjustKValue(float current_acceptance_rate);

    // 智能调整K值 (考虑温度和近期趋势)
    void smartAdjustK(float temperature);

    // 检查是否应该降级到贪婪解码 (early stopping)
    bool shouldFallbackToGreedy() const;

    // 辅助方法: 获取token概率
    float getTokenProbability(llama_context* ctx, int token, int pos) const;

    // 辅助方法: 采样下一个token
    int sampleToken(llama_context* ctx, float temperature) const;

    // ============ 成员变量 ============

    SpeculativeConfig config_;  // 配置
    SpeculativeStats stats_;    // 统计信息

    // Drafter模型资源
    llama_model* draft_model_ = nullptr;
    llama_context* draft_ctx_ = nullptr;

    // 互斥锁 (保护draft_ctx_的并发访问)
    mutable std::mutex draft_mutex_;
};

// ============================================================
// 工具函数
// ============================================================

// 时间测量辅助类 (RAII)
class ScopedTimer {
public:
    explicit ScopedTimer(std::atomic<uint64_t>& counter)
        : counter_(counter), start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::steady_clock::now();
        auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
        counter_.fetch_add(duration_ns, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t>& counter_;
    std::chrono::steady_clock::time_point start_;
};
