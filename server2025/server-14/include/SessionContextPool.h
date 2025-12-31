#ifndef SESSION_CONTEXT_POOL_H
#define SESSION_CONTEXT_POOL_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "llama.h"

/**
 * Session Context Pool - 会话级 KV 缓存池
 *
 * 核心优化：为每个会话维护专属的 llama_context，复用 KV 缓存
 *
 * 性能提升：
 * - 第一轮对话：100 tokens → 0.5s
 * - 续写对话：只需处理新增 20 tokens → 0.1s （5倍提升）
 * - 避免重复计算已有上下文的 KV 缓存
 *
 * 设计理念：
 * - Session 绑定：每个 session_id 对应一个持久化的 context
 * - KV 缓存保留：context 中的 KV 缓存在推理间持久保存
 * - 增量推理：只处理新增的 tokens
 * - LRU 淘汰：资源有限时淘汰最久未使用的会话
 */
class SessionContextPool {
public:
    /**
     * 会话上下文结构
     */
    struct SessionContext {
        llama_context* ctx;              // 专属 context（包含 KV 缓存）
        std::vector<llama_token> tokens; // 已处理的完整 token 序列
        int cached_token_count;          // 当前 KV 缓存中的 token 数量
        long long last_used;             // 最后使用时间（毫秒时间戳）

        SessionContext()
            : ctx(nullptr), cached_token_count(0), last_used(0) {}

        SessionContext(llama_context* c)
            : ctx(c), cached_token_count(0),
              last_used(getCurrentTime()) {}

        static long long getCurrentTime() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        }

        void updateLastUsed() {
            last_used = getCurrentTime();
        }
    };

    /**
     * 构造函数
     * @param model llama_model 指针（生命周期由外部管理）
     * @param max_sessions 最大会话数量（超出时触发 LRU 淘汰）
     * @param n_ctx 上下文长度
     * @param n_threads 推理线程数
     */
    SessionContextPool(llama_model* model, int max_sessions, uint32_t n_ctx, int n_threads);

    /**
     * 析构函数 - 释放所有 session context
     */
    ~SessionContextPool();

    // 禁用拷贝和赋值
    SessionContextPool(const SessionContextPool&) = delete;
    SessionContextPool& operator=(const SessionContextPool&) = delete;

    /**
     * 获取或创建 session 的 context
     *
     * @param session_id 会话 ID
     * @return SessionContext* 成功返回会话上下文，失败返回 nullptr
     *
     * 行为：
     * - 如果 session 已存在，返回已有的 context（包含 KV 缓存）
     * - 如果 session 不存在，创建新的 context
     * - 如果达到最大 session 数，先执行 LRU 淘汰
     */
    SessionContext* getOrCreateSession(const std::string& session_id);

    /**
     * 增量推理：基于已缓存的 KV，只处理新增 tokens
     *
     * @param session_id 会话 ID
     * @param new_tokens 新的完整 token 序列（包括历史）
     * @param prompt 完整 prompt（用于日志）
     * @return bool 成功返回 true，失败返回 false
     *
     * 核心算法：
     * 1. 找到新旧 token 序列的公共前缀
     * 2. 只对增量部分进行 llama_decode
     * 3. 更新 cached_token_count
     *
     * 示例：
     * - 第一轮：tokens = [1,2,3,4,5]，全部推理
     * - 第二轮：tokens = [1,2,3,4,5,6,7]，只推理 [6,7]（复用前5个token的KV）
     */
    bool processIncrementalTokens(
        const std::string& session_id,
        const std::vector<llama_token>& new_tokens,
        const std::string& prompt = ""
    );

    /**
     * 删除指定会话（释放其 context 和 KV 缓存）
     * @param session_id 要删除的会话 ID
     */
    void deleteSession(const std::string& session_id);

    /**
     * 清空所有会话
     */
    void clearAllSessions();

    /**
     * 获取池统计信息
     */
    struct PoolStats {
        int total_sessions;        // 当前总会话数
        int max_sessions;          // 最大会话数限制
        uint64_t total_creates;    // 总创建次数
        uint64_t total_evictions;  // 总淘汰次数
        uint64_t total_hits;       // 缓存命中次数（复用KV）
        uint64_t total_misses;     // 缓存未命中次数（新建）
    };

    PoolStats getStats() const;

private:
    // 模型指针（不拥有所有权）
    llama_model* model_;

    // Context 参数
    uint32_t n_ctx_;
    int n_threads_;
    int max_sessions_;

    // Session -> Context 映射
    std::unordered_map<std::string, SessionContext> session_map_;
    mutable std::mutex mutex_;

    // 统计信息
    uint64_t total_creates_;
    uint64_t total_evictions_;
    uint64_t total_hits_;      // 复用已有 session
    uint64_t total_misses_;    // 创建新 session

    /**
     * 创建一个新的 llama_context
     */
    llama_context* createContext();

    /**
     * LRU 淘汰策略：删除最久未使用的会话
     */
    void evictLRU();
};

#endif // SESSION_CONTEXT_POOL_H
