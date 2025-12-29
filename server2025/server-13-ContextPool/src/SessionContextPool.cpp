#include "SessionContextPool.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>
#include <climits>

SessionContextPool::SessionContextPool(
    llama_model* model,
    int max_sessions,
    uint32_t n_ctx,
    int n_threads
)
    : model_(model),
      n_ctx_(n_ctx),
      n_threads_(n_threads),
      max_sessions_(max_sessions),
      total_creates_(0),
      total_evictions_(0),
      total_hits_(0),
      total_misses_(0)
{
    if (!model_) {
        throw std::invalid_argument("SessionContextPool: model cannot be null");
    }

    if (max_sessions <= 0) {
        throw std::invalid_argument("SessionContextPool: max_sessions must be > 0");
    }

    std::cout << "[SessionContextPool] Initialized with max_sessions="
              << max_sessions
              << ", n_ctx=" << n_ctx
              << ", n_threads=" << n_threads << std::endl;
}

SessionContextPool::~SessionContextPool() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << "[SessionContextPool] Destroying pool, freeing "
              << session_map_.size() << " session contexts..." << std::endl;

    // 释放所有 context
    for (auto& [session_id, session_ctx] : session_map_) {
        if (session_ctx.ctx) {
            llama_free(session_ctx.ctx);
        }
    }

    session_map_.clear();

    std::cout << "[SessionContextPool] Pool destroyed" << std::endl;
}

llama_context* SessionContextPool::createContext() {
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;
    cp.n_threads = n_threads_;
    cp.n_batch = 512;

    llama_context* ctx = llama_new_context_with_model(model_, cp);
    return ctx;
}

void SessionContextPool::evictLRU() {
    // 找到最久未使用的 session
    std::string lru_session_id;
    long long min_last_used = LLONG_MAX;

    for (const auto& [session_id, session_ctx] : session_map_) {
        if (session_ctx.last_used < min_last_used) {
            min_last_used = session_ctx.last_used;
            lru_session_id = session_id;
        }
    }

    if (!lru_session_id.empty()) {
        std::cout << "[SessionContextPool] LRU evicting session: "
                  << lru_session_id.substr(0, 12) << "..." << std::endl;

        // 释放 context
        auto it = session_map_.find(lru_session_id);
        if (it != session_map_.end() && it->second.ctx) {
            llama_free(it->second.ctx);
        }

        session_map_.erase(lru_session_id);
        total_evictions_++;
    }
}

SessionContextPool::SessionContext* SessionContextPool::getOrCreateSession(
    const std::string& session_id
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 查找已有 session
    auto it = session_map_.find(session_id);
    if (it != session_map_.end()) {
        // 缓存命中
        it->second.updateLastUsed();
        total_hits_++;
        return &(it->second);
    }

    // 缓存未命中，需要创建新 session
    total_misses_++;

    // 检查是否需要淘汰
    if ((int)session_map_.size() >= max_sessions_) {
        evictLRU();
    }

    // 创建新 context
    llama_context* ctx = createContext();
    if (!ctx) {
        std::cerr << "[SessionContextPool] Failed to create context for session: "
                  << session_id << std::endl;
        return nullptr;
    }

    // 添加到 map
    SessionContext session_ctx(ctx);
    auto result = session_map_.emplace(session_id, session_ctx);

    total_creates_++;

    std::cout << "[SessionContextPool] Created new session: "
              << session_id.substr(0, 12) << "... "
              << "(total: " << session_map_.size() << ")" << std::endl;

    return &(result.first->second);
}

bool SessionContextPool::processIncrementalTokens(
    const std::string& session_id,
    const std::vector<llama_token>& new_tokens,
    const std::string& prompt
) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 获取 session context
    auto it = session_map_.find(session_id);
    if (it == session_map_.end()) {
        std::cerr << "[SessionContextPool] Session not found: " << session_id << std::endl;
        return false;
    }

    SessionContext& session_ctx = it->second;
    llama_context* ctx = session_ctx.ctx;

    if (!ctx) {
        std::cerr << "[SessionContextPool] Context is null for session: " << session_id << std::endl;
        return false;
    }

    // 更新最后使用时间
    session_ctx.updateLastUsed();

    // 找到公共前缀长度
    const auto& old_tokens = session_ctx.tokens;
    size_t common_prefix_len = 0;

    for (size_t i = 0; i < std::min(old_tokens.size(), new_tokens.size()); ++i) {
        if (old_tokens[i] == new_tokens[i]) {
            common_prefix_len++;
        } else {
            break;
        }
    }

    // 计算需要推理的增量部分
    int tokens_to_process = (int)new_tokens.size() - (int)common_prefix_len;

    if (tokens_to_process <= 0) {
        // 没有新增 tokens，无需推理
        std::cout << "[SessionContextPool] No new tokens to process for session: "
                  << session_id.substr(0, 12) << "..." << std::endl;
        return true;
    }

    std::cout << "[SessionContextPool] Session " << session_id.substr(0, 12) << "..."
              << " - Reusing " << common_prefix_len << " tokens, "
              << "processing " << tokens_to_process << " new tokens" << std::endl;

    // 智能处理KV缓存不一致的情况
    if ((int)common_prefix_len != session_ctx.cached_token_count) {
        std::cout << "[SessionContextPool] KV cache mismatch for session "
                  << session_id.substr(0, 12) << "..."
                  << " (common_prefix=" << common_prefix_len
                  << ", cached=" << session_ctx.cached_token_count << ")" << std::endl;

        if ((int)common_prefix_len < session_ctx.cached_token_count) {
            // 情况1: KV缓存比公共前缀多（用户可能编辑了历史或回退）
            // 由于旧版llama.cpp不支持llama_kv_cache_seq_rm，需要重建context
            std::cout << "[SessionContextPool] Rebuilding context due to excess KV cache" << std::endl;

            if (session_ctx.ctx) {
                llama_free(session_ctx.ctx);
            }
            session_ctx.ctx = createContext();
            ctx = session_ctx.ctx;

            // 重新处理所有tokens
            common_prefix_len = 0;
            tokens_to_process = (int)new_tokens.size();
            session_ctx.cached_token_count = 0;

        } else {
            // 情况2: KV缓存比公共前缀少（可能是上次推理中断或出错）
            // 从KV缓存的位置继续推理
            std::cout << "[SessionContextPool] KV cache incomplete, will resume from position "
                      << session_ctx.cached_token_count << std::endl;

            // 调整公共前缀长度为实际的KV缓存长度
            common_prefix_len = session_ctx.cached_token_count;
            tokens_to_process = (int)new_tokens.size() - (int)common_prefix_len;
        }
    }

    // 创建 batch 处理增量 tokens
    llama_batch batch = llama_batch_init(tokens_to_process, 0, 1);

    for (int i = 0; i < tokens_to_process; ++i) {
        int token_idx = (int)common_prefix_len + i;
        batch.token[i] = new_tokens[token_idx];
        batch.pos[i] = token_idx;
        batch.seq_id[i][0] = 0;
        batch.n_seq_id[i] = 1;
        batch.logits[i] = (i == tokens_to_process - 1);  // 只有最后一个 token 需要 logits
    }
    batch.n_tokens = tokens_to_process;

    // 执行推理
    int ret = llama_decode(ctx, batch);
    llama_batch_free(batch);

    if (ret != 0) {
        std::cerr << "[SessionContextPool] llama_decode failed for session: "
                  << session_id << ", ret=" << ret << std::endl;
        return false;
    }

    // 更新 session context 状态
    session_ctx.tokens = new_tokens;
    session_ctx.cached_token_count = (int)new_tokens.size();

    return true;
}

void SessionContextPool::deleteSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = session_map_.find(session_id);
    if (it != session_map_.end()) {
        if (it->second.ctx) {
            llama_free(it->second.ctx);
        }
        session_map_.erase(it);

        std::cout << "[SessionContextPool] Deleted session: "
                  << session_id.substr(0, 12) << "..." << std::endl;
    }
}

void SessionContextPool::clearAllSessions() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [session_id, session_ctx] : session_map_) {
        if (session_ctx.ctx) {
            llama_free(session_ctx.ctx);
        }
    }

    session_map_.clear();

    std::cout << "[SessionContextPool] Cleared all sessions" << std::endl;
}

SessionContextPool::PoolStats SessionContextPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    PoolStats stats;
    stats.total_sessions = (int)session_map_.size();
    stats.max_sessions = max_sessions_;
    stats.total_creates = total_creates_;
    stats.total_evictions = total_evictions_;
    stats.total_hits = total_hits_;
    stats.total_misses = total_misses_;

    return stats;
}
