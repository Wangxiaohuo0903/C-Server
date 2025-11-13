#pragma once

#include "llama.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

/**
 * @brief 推测式解码器（Speculative Decoding）
 *
 * 核心原理：
 * 1. 使用小模型（draft model）快速生成 N 个候选 tokens
 * 2. 使用大模型（target model）并行验证这些 tokens
 * 3. 接受正确的 tokens，拒绝错误的 tokens
 * 4. 理论加速比：1.5x - 3.0x（取决于接受率）
 *
 * 使用示例：
 * ```cpp
 * SpeculativeDecoder decoder(model_tgt, ctx_tgt, "path/to/draft.gguf", config);
 * std::string result = decoder.infer(prompt, 100, 0.7f);
 * auto stats = decoder.getStats();
 * std::cout << "Accept rate: " << stats.accept_rate << std::endl;
 * std::cout << "Speedup: " << stats.speedup << "x" << std::endl;
 * ```
 */
class SpeculativeDecoder {
public:
    // ============ 配置参数 ============
    struct Config {
        // Draft 生成参数
        int n_draft = 16;           // 每次生成的 draft tokens 数量（推荐 8-32）
        int n_draft_min = 5;        // 最小 draft 数量（低于此值跳过 draft）
        float p_min = 0.9f;         // Draft 置信度阈值（低于此值停止 draft）

        // Draft 模型参数
        int n_ctx_draft = 2048;     // Draft 模型上下文长度
        int n_threads_draft = 2;    // Draft 模型线程数（通常少于 target）
        int n_gpu_layers_draft = 0; // Draft 模型 GPU 层数（0=纯CPU）

        // 采样参数
        float temperature = 0.7f;   // 温度参数
        int top_k = 40;            // Top-K 采样
        float top_p = 0.9f;        // Top-P (nucleus) 采样

        // 性能优化
        bool enable_kv_reuse = true; // 是否复用 KV cache
        bool verbose = false;        // 是否打印详细日志
    };

    // ============ 性能统计 ============
    struct Stats {
        // 基础统计
        uint64_t n_predict = 0;     // 总共生成的 tokens 数
        uint64_t n_drafted = 0;     // 总共 draft 的 tokens 数
        uint64_t n_accepted = 0;    // 被接受的 draft tokens 数

        // 性能指标
        double accept_rate = 0.0;   // 接受率 = n_accepted / n_drafted
        double speedup = 0.0;       // 加速比（相对于传统自回归）

        // 时间统计
        double time_draft_ms = 0.0;   // Draft 阶段总耗时（毫秒）
        double time_verify_ms = 0.0;  // Verify 阶段总耗时（毫秒）
        double time_total_ms = 0.0;   // 总耗时（毫秒）

        // 每 token 平均时间
        double ms_per_token() const {
            return n_predict > 0 ? time_total_ms / n_predict : 0.0;
        }

        // 吞吐量（tokens/秒）
        double tokens_per_sec() const {
            return time_total_ms > 0 ? (n_predict * 1000.0) / time_total_ms : 0.0;
        }
    };

    // ============ 构造与析构 ============

    /**
     * @brief 构造推测式解码器
     *
     * @param model_target 目标模型（大模型）
     * @param ctx_target 目标模型上下文
     * @param draft_model_path Draft 模型路径（小模型）
     * @param config 配置参数
     */
    SpeculativeDecoder(
        llama_model* model_target,
        llama_context* ctx_target,
        const std::string& draft_model_path,
        const Config& config = Config()
    );

    ~SpeculativeDecoder();

    // 禁止拷贝和移动
    SpeculativeDecoder(const SpeculativeDecoder&) = delete;
    SpeculativeDecoder& operator=(const SpeculativeDecoder&) = delete;

    // ============ 推理接口 ============

    /**
     * @brief 使用推测式解码进行推理
     *
     * @param prompt 输入提示词
     * @param max_tokens 最大生成 token 数
     * @param temperature 温度参数（覆盖 config 中的值）
     * @return 生成的文本
     */
    std::string infer(
        const std::string& prompt,
        int max_tokens,
        float temperature = -1.0f  // -1 表示使用 config 中的值
    );

    /**
     * @brief 使用推测式解码进行推理（token 级接口）
     *
     * @param prompt_tokens 输入 tokens
     * @param max_tokens 最大生成 token 数
     * @param temperature 温度参数
     * @return 生成的 tokens
     */
    std::vector<llama_token> inferTokens(
        const std::vector<llama_token>& prompt_tokens,
        int max_tokens,
        float temperature = -1.0f
    );

    // ============ 统计与监控 ============

    /**
     * @brief 获取性能统计信息
     */
    Stats getStats() const;

    /**
     * @brief 重置统计信息
     */
    void resetStats();

    /**
     * @brief 打印统计信息
     */
    void printStats() const;

    // ============ 配置管理 ============

    /**
     * @brief 获取当前配置
     */
    const Config& getConfig() const { return config_; }

    /**
     * @brief 更新配置（运行时修改）
     */
    void updateConfig(const Config& config) { config_ = config; }

    /**
     * @brief 检查 draft 模型是否已加载
     */
    bool isDraftModelLoaded() const { return model_dft_ != nullptr && ctx_dft_ != nullptr; }

private:
    // ============ 内部状态 ============

    // Target model（大模型）
    llama_model* model_tgt_;
    llama_context* ctx_tgt_;

    // Draft model（小模型）
    llama_model* model_dft_;
    llama_context* ctx_dft_;

    // 配置与统计
    Config config_;
    mutable Stats stats_;
    mutable std::mutex stats_mutex_;

    // ============ 核心算法 ============

    /**
     * @brief Draft 阶段：使用小模型生成候选 tokens
     *
     * @param prompt 当前 prompt tokens
     * @param last_token 上一个生成的 token
     * @param n_past 已处理的 token 数量
     * @return Draft tokens 序列
     */
    std::vector<llama_token> genDraft(
        const std::vector<llama_token>& prompt,
        llama_token last_token,
        int n_past
    );

    /**
     * @brief Verify 阶段：使用大模型验证并接受 tokens
     *
     * @param draft Draft tokens 序列
     * @param last_token 上一个生成的 token
     * @param n_past 已处理的 token 数量
     * @return 被接受的 tokens
     */
    std::vector<llama_token> verifyAndAccept(
        const std::vector<llama_token>& draft,
        llama_token last_token,
        int n_past
    );

    // ============ 采样辅助函数 ============

    /**
     * @brief 从 logits 中采样一个 token
     */
    llama_token sampleToken(
        llama_context* ctx,
        float temperature,
        int top_k,
        float top_p
    );

    /**
     * @brief 贪婪采样（最大概率）
     */
    llama_token sampleGreedy(llama_context* ctx);

    /**
     * @brief 获取 token 的概率
     */
    float getTokenProb(llama_context* ctx, llama_token token);

    // ============ Tokenization ============

    /**
     * @brief 将文本转换为 tokens
     */
    std::vector<llama_token> tokenize(const std::string& text) const;

    /**
     * @brief 将 token 转换为文本
     */
    std::string detokenize(llama_token token) const;

    /**
     * @brief 将 tokens 序列转换为文本
     */
    std::string detokenize(const std::vector<llama_token>& tokens) const;

    // ============ 时间测量 ============

    using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;

    TimePoint now() const {
        return std::chrono::steady_clock::now();
    }

    double elapsedMs(TimePoint start, TimePoint end) const {
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

/**
 * @brief 推测式解码工厂函数
 *
 * 便捷创建推测式解码器的函数
 */
inline std::unique_ptr<SpeculativeDecoder> createSpeculativeDecoder(
    llama_model* model_target,
    llama_context* ctx_target,
    const std::string& draft_model_path,
    const SpeculativeDecoder::Config& config = SpeculativeDecoder::Config()
) {
    return std::make_unique<SpeculativeDecoder>(
        model_target, ctx_target, draft_model_path, config
    );
}
