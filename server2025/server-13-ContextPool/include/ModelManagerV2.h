#ifndef MODEL_MANAGER_V2_H
#define MODEL_MANAGER_V2_H

#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include "llama.h"
#include "SessionContextPool.h"
#include "BatchInferenceEngine.h"

// 流式输出回调函数类型
// 参数: token文本片段
// 返回: true继续生成，false停止生成
using StreamCallback = std::function<bool(const std::string&)>;

/**
 * ModelManager V2 - 增强版模型管理器
 *
 * Server-13 核心优化：
 * 1. ✅ KV 缓存复用（SessionContextPool）
 * 2. ✅ 批处理推理（BatchInferenceEngine）
 * 3. ✅ 线程安全
 *
 * 相比 V1 的改进：
 * - V1: 每次推理创建临时 context，重新计算完整 prompt 的 KV 缓存
 * - V2: 为每个 session 维护持久 context，只计算增量 tokens
 *
 * 性能提升（10轮对话）：
 * - V1: 100 tokens × 10 = 1000 tokens 计算，耗时 5s
 * - V2: 100 + 20×9 = 280 tokens 计算，耗时 1.4s（3.6倍提升）
 */
class ModelManagerV2 {
public:
    /**
     * 获取单例实例
     */
    static ModelManagerV2& instance();

    /**
     * 加载模型
     *
     * @param path 模型文件路径（.gguf格式）
     * @param n_ctx 最大上下文长度
     * @param n_threads CPU推理线程数
     * @param max_sessions 最大会话数（SessionContextPool）
     * @param enable_batch 是否启用批处理推理
     * @param batch_size 批处理大小
     * @return 成功返回 true，失败返回 false
     */
    bool loadModel(
        const std::string& path,
        int n_ctx = 2048,
        int n_threads = 4,
        int max_sessions = 100,
        bool enable_batch = false,
        int batch_size = 4
    );

    /**
     * 带 KV 缓存复用的推理（推荐使用）
     *
     * @param session_id 会话 ID
     * @param prompt 完整 prompt（包含上下文）
     * @param tokens 完整 token 序列（如果已 tokenize）
     * @param max_tokens 最大生成 token 数
     * @param temperature 采样温度
     * @return 生成的文本
     *
     * 核心优化：
     * - 自动检测 token 序列的公共前缀
     * - 只推理增量部分
     * - 保留 KV 缓存供下次使用
     */
    std::string inferWithCache(
        const std::string& session_id,
        const std::string& prompt,
        const std::vector<llama_token>& tokens,
        int max_tokens,
        float temperature
    );

    /**
     * 简化版接口（自动 tokenize）
     */
    std::string inferWithCache(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature
    );

    /**
     * 流式推理接口（实时输出每个 token）
     *
     * @param session_id 会话 ID
     * @param prompt 完整 prompt
     * @param max_tokens 最大生成 token 数
     * @param temperature 采样温度
     * @param callback 流式输出回调函数，每生成一个token就调用一次
     * @return 完整生成的文本
     *
     * 特性：
     * - 支持 KV 缓存复用
     * - 每生成一个 token 立即通过回调函数返回
     * - 适合 SSE (Server-Sent Events) 流式输出
     */
    std::string inferWithCacheStreaming(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature,
        StreamCallback callback
    );

    /**
     * 批处理推理接口（异步）
     *
     * @param session_id 会话 ID
     * @param prompt 完整 prompt
     * @param max_tokens 最大生成 token 数
     * @param temperature 采样温度
     * @return std::future<std::string> 异步结果
     *
     * 注意：只有在 enable_batch=true 时可用
     */
    std::future<std::string> inferBatch(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature
    );

    /**
     * 原始推理接口（无缓存，兼容旧代码）
     *
     * 注意：不推荐使用，性能较差
     */
    std::string raw_infer(
        const std::string& prompt,
        int max_tokens,
        float temperature
    ) const;

    /**
     * 删除会话（释放其 KV 缓存）
     */
    void deleteSession(const std::string& session_id);

    /**
     * Tokenize 文本
     */
    std::vector<llama_token> tokenize(const std::string& text, bool add_bos = true) const;

    /**
     * Token 转文本
     */
    std::string detokenize(const std::vector<llama_token>& tokens) const;

    /**
     * 获取模型词表
     */
    const llama_vocab* getVocab() const;

    /**
     * 获取统计信息
     */
    struct Stats {
        SessionContextPool::PoolStats session_pool_stats;
        BatchInferenceEngine::BatchStats batch_stats;
        bool batch_enabled;
    };

    Stats getStats() const;

private:
    // 单例模式
    ModelManagerV2();
    ~ModelManagerV2();
    ModelManagerV2(const ModelManagerV2&) = delete;
    ModelManagerV2& operator=(const ModelManagerV2&) = delete;

    // 模型资源
    llama_model* model_;
    mutable std::mutex model_mutex_;

    // Context 参数
    int n_ctx_;
    int n_threads_;

    // SessionContextPool（KV 缓存复用）
    std::unique_ptr<SessionContextPool> session_pool_;

    // BatchInferenceEngine（批处理推理）
    std::unique_ptr<BatchInferenceEngine> batch_engine_;
    bool batch_enabled_;

    /**
     * 生成 tokens（内部使用）
     *
     * @param ctx llama_context
     * @param max_tokens 最大生成 token 数
     * @param temperature 采样温度
     * @param n_past 已处理的 token 数量（起始位置）
     * @return 生成的文本
     */
    std::string generateTokens(
        llama_context* ctx,
        int max_tokens,
        float temperature,
        int n_past = 0
    ) const;

    /**
     * 流式生成 tokens（支持回调）
     *
     * @param ctx llama context
     * @param max_tokens 最大生成 token 数
     * @param temperature 采样温度
     * @param n_past 已处理的 token 数量
     * @param callback 每生成一个token时的回调函数
     * @return 生成的文本
     */
    std::string generateTokensStreaming(
        llama_context* ctx,
        int max_tokens,
        float temperature,
        int n_past,
        StreamCallback callback
    ) const;
};

#endif // MODEL_MANAGER_V2_H
