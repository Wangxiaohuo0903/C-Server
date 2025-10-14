#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <sstream>
#include <chrono>
#include <cstdint>

// ------------------ 对话消息结构 ------------------
// 每条消息包含角色（user/assistant/system）和内容
struct Message {
    std::string role;     // "user" | "assistant" | "system"
    std::string content;  // 消息文本
};

// ------------------ 会话管理 ------------------
class ChatSession {
public:
    // 向会话追加一条消息，并在必要时自动裁剪历史
    void add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        prune();   // 限制上下文长度，避免 prompt 过长
    }

    // 将当前历史序列化为 DeepSeek/ChatML 格式 Prompt
    std::string makePrompt() const {
        std::ostringstream oss;
        // 对每条消息前后加上标记，ChatML 协议格式
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n"
                << m.content << "\n";
        }
        // 最后留给模型接着写 assistant 部分
        oss << "<|assistant|>\n";
        return oss.str();
    }

    // 完全清空历史（例如 reset 会话时调用）
    void reset() {
        history.clear();
    }

private:
    // 会话历史按时间先后顺序存储
    std::vector<Message> history;

    // ============ 内部：裁剪历史 ============
    // 当历史过长时，保留最后几轮；最旧的合并成“system”摘要
    void prune() {
        const int max_round_keep       = 4;   // 最多保留完整的 4 轮
        const int max_tokens_per_msg   = 200; // 单条消息内容最大字符数

        // 1) 逐条截断超长文本，避免单条消息撑爆 prompt
        for (auto& m : history) {
            if ((int)m.content.size() > max_tokens_per_msg) {
                m.content.resize(max_tokens_per_msg);
            }
        }

        // 2) 超过总条数时，进行“摘要+删除最早两条”的简化处理
        //    保证上下文窗口大小可控，同时保留一定的历史概览
        while ((int)history.size() > max_round_keep * 2) {
            // 这里简易实现：直接丢弃最早的一轮（两条消息），
            // 并在开头插入一条 system 角色的摘要提示
            std::string summary = "[summary] previous conversation ...";
            history.erase(history.begin(), history.begin() + 2);
            history.insert(history.begin(), {"system", summary});
        }
    }
};

// ------------------ 前缀缓存条目 ------------------
struct PrefixCacheEntry {
    std::string prefix_text;      // 前缀文本（用于匹配）
    int seq_id;                   // llama.cpp 序列ID
    int kv_length;                // 前缀对应的token数量
    uint64_t last_used_ns;        // 最后使用时间（纳秒）
    uint32_t hit_count;           // 命中次数

    PrefixCacheEntry()
        : seq_id(0), kv_length(0), last_used_ns(0), hit_count(0) {}

    PrefixCacheEntry(const std::string& text, int sid, int len)
        : prefix_text(text), seq_id(sid), kv_length(len),
          last_used_ns(getCurrentTimeNs()), hit_count(1) {}

    // 获取当前时间（纳秒）
    static uint64_t getCurrentTimeNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
};

class ModelManager {
public:
    // 单例接口：全局共享一个 ModelManager 实例
    static ModelManager& instance();

    // 加载或重载模型（线程安全）
    // path: 模型文件路径
    // n_ctx: 最大上下文长度；n_threads: 并行线程数
    bool loadModel(const std::string& path,
                   int n_ctx = 2048,
                   int n_threads = 4);

    // 多轮对话接口
    // chat_id: 会话标识，用于索引 ChatSession
    // user_msg: 本轮用户输入
    // maxTokens, temperature: 解码参数
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens,
                      float temperature);

    // 清理指定会话历史
    void dropSession(const std::string& chat_id);

    // ============ 前缀缓存接口 ============

    // 查找前缀缓存，如果命中返回已缓存的token数量，否则返回0
    // prefix: 前缀文本
    // 返回: 命中的token数量（0表示未命中）
    int findPrefixCache(const std::string& prefix);

    // 保存前缀到缓存
    // prefix: 前缀文本
    // kv_length: 该前缀对应的token数量
    void savePrefixCache(const std::string& prefix, int kv_length);

    // 获取缓存统计信息
    struct CacheStats {
        size_t cache_size;        // 当前缓存条目数
        uint64_t total_hits;      // 总命中次数
        uint64_t total_requests;  // 总请求次数
        double hit_rate;          // 命中率
    };
    CacheStats getCacheStats() const;

    // 清空所有前缀缓存
    void clearPrefixCache();

private:
    ModelManager();
    ~ModelManager();
    ModelManager(const ModelManager&)            = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    // 保护 loadModel / infer 接口的互斥锁
    std::mutex mtx_;

    // 底层 llama.cpp 模型句柄
    struct llama_model* model_     = nullptr;
    mutable struct llama_context* ctx_   = nullptr;  // mutable 因为 raw_infer 需要修改 KV cache
    int n_ctx_     = 2048;
    int n_threads_ = 4;

    // 保存所有活跃的 ChatSession，key 为 chat_id
    std::unordered_map<std::string, ChatSession> chat_sessions_;
    std::mutex chat_mutex_;

    // ============ 前缀缓存相关 ============
    // 前缀缓存池：key为前缀文本，value为缓存条目
    std::unordered_map<std::string, PrefixCacheEntry> prefix_cache_;
    mutable std::mutex cache_mutex_;  // mutable因为getCacheStats是const

    // 序列ID分配（seq_id=0留给当前推理）
    int next_seq_id_ = 1;

    // 缓存限制
    static constexpr size_t MAX_PREFIX_CACHE = 32;  // 最多缓存32个前缀

    // 统计信息
    mutable uint64_t total_cache_hits_ = 0;
    mutable uint64_t total_cache_requests_ = 0;

    // LRU淘汰：移除最久未使用的缓存条目
    void evictLRU();

    // **内部**单轮推理：不使用历史，只按给定 prompt 一次性生成
    // prefix_kv_len: 如果>0，表示前prefix_kv_len个token已在KV cache中
    std::string raw_infer(const std::string& prompt,
                          int maxTokens,
                          float temperature,
                          int prefix_kv_len = 0) const;
};
