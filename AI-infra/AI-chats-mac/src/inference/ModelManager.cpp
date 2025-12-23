#include "ModelManager.h"
#include "SpeculativeDecoder.h"
#include "llama.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// ---------- ctor / dtor ----------
ModelManager::ModelManager()  = default;
ModelManager::~ModelManager() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_model_free(model_);
}

// ---------- singleton ----------
ModelManager& ModelManager::instance() {
    static ModelManager inst;
    return inst;
}

// ---------- loadModel ----------
bool ModelManager::loadModel(const std::string& path, int n_ctx, int n_threads) {
    std::lock_guard<std::mutex> g(mtx_);

    // 释放旧的 context 和 model
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }

    // 加载新模型
    llama_model_params mp = llama_model_default_params();
    model_ = llama_model_load_from_file(path.c_str(), mp);
    if (!model_) {
        std::cerr << "[Model] load failed: " << path << '\n';
        return false;
    }

    // 创建持久的 context
    n_ctx_ = n_ctx;
    n_threads_ = n_threads;
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;
    cp.n_threads = n_threads_;
    ctx_ = llama_init_from_model(model_, cp);

    if (!ctx_) {
        std::cerr << "[Model] context creation failed\n";
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    std::cout << "[Model] loaded ok: " << path << '\n';
    return true;
}

// =========== 单轮推理（底层，无历史） ===========
std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature, int prefix_kv_len) const {
    // 打印 prompt 长度
    std::cerr << "[run] prompt bytes=" << prompt.size() << "  maxTok=" << maxTokens << "  prefix_kv=" << prefix_kv_len << '\n';

    // 0. 使用持久的 context
    if (!ctx_) return "[no_ctx]";

    // 如果没有前缀缓存，清空 KV cache；否则保留前缀部分
    if (prefix_kv_len == 0) {
        // 使用新的 memory API 清空 KV cache
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_clear(mem, true);  // 清空数据和元数据
        std::cerr << "[run] KV cache cleared using new memory API\n";
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // 1. tokenize 完整prompt
    std::vector<llama_token> tokBuf(prompt.size() * 4);
    int nTok = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.size(),
        tokBuf.data(), (int)tokBuf.size(),
        true, false
    );
    if (nTok < 1) { return "[tok_fail]"; }
    tokBuf.resize(nTok);
    std::cerr << "[run] nTok=" << nTok << '\n';

    // 2. 推 prompt（跳过已缓存的前缀部分）
    int start_idx = (prefix_kv_len > 0 && prefix_kv_len < nTok) ? prefix_kv_len : 0;
    int tokens_to_process = nTok - start_idx;

    if (tokens_to_process > 0) {
        std::cerr << "[run] processing tokens [" << start_idx << ", " << nTok << "), count=" << tokens_to_process << '\n';

        llama_batch full = llama_batch_init(tokens_to_process, 0, 1);
        for (int i = 0; i < tokens_to_process; ++i) {
            int tok_idx = start_idx + i;
            full.token[i]     = tokBuf[tok_idx];
            full.pos[i]       = tok_idx;
            full.seq_id[i][0] = 0;
            full.n_seq_id[i]  = 1;
            full.logits[i]    = (i == tokens_to_process - 1);
        }
        full.n_tokens = tokens_to_process;
        if (llama_decode(ctx_, full) != 0) {
            llama_batch_free(full);
            return "[decode_prompt_fail]";
        }
        llama_batch_free(full);
    } else {
        std::cerr << "[run] fully cached! skip prompt processing\n";
    }

    // 3. 生成
    int nPast = nTok;
    const int eos   = llama_vocab_eos(vocab);
    const int vSize = llama_vocab_n_tokens(vocab);
    std::string out; out.reserve(maxTokens * 4);

    llama_batch bGen = llama_batch_init(1, 0, 1);

    // 采样配置
    const float top_p = 0.9f;
    const int   top_k = 40;
    const float temp  = temperature > 0 ? temperature : 0.7f;

   for (int step = 0; step < maxTokens; ++step) {
    const float* logits = llama_get_logits(ctx_);
    if (!logits) break;

    // 贪婪采样（最大概率）
    int   best = 0;
    float bestv = logits[0];
    for (int v = 1; v < vSize; ++v)
        if (logits[v] > bestv) { bestv = logits[v]; best = v; }

    char piece[256] = {0};
    llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
    // std::cerr << "[run] step " << step << " tok " << best << " \"" << piece << "\"\n";  // 注释掉减少日志输出

    // 停止条件
    if (best == eos || piece[0] == '<')
        break;

    out += piece;

    // 填 batch
    bGen.n_tokens     = 1;
    bGen.token[0]     = best;
    bGen.pos[0]       = nPast;
    bGen.seq_id[0][0] = 0;
    bGen.n_seq_id[0]  = 1;
    bGen.logits[0]    = 1;

    if (llama_decode(ctx_, bGen) != 0) break;
    ++nPast;
}

    llama_batch_free(bGen);

    std::cerr << "[run] done, out.len=" << out.size() << '\n';
    return out;
}

// =========== 多轮对话（维护历史/自动构造prompt） ===========
std::string ModelManager::infer(const std::string& chat_id, const std::string& user_msg, int maxTokens, float temperature) {
    // 1. 保存用户消息
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        chat_sessions_[chat_id].add("user", user_msg);
    }

    // 2. 构造prompt
    std::string prompt;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        prompt = chat_sessions_[chat_id].makePrompt();
    }

    // ============ 前缀缓存优化 v2 ============
    // 新策略：只缓存第一轮完整对话（到第一个assistant回复结束）
    // 这样相同chat_id的所有后续请求都能复用相同的前缀
    std::string prefix;
    int prefix_kv_len = 0;

    // 查找第一个 <|assistant|> 和第二个 <|user|>
    size_t first_assistant_pos = prompt.find("<|assistant|>");
    if (first_assistant_pos != std::string::npos) {
        // 找第二个 <|user|>（在第一个assistant之后）
        size_t second_user_pos = prompt.find("<|user|>", first_assistant_pos + 13);

        if (second_user_pos != std::string::npos) {
            // 有至少2轮对话，提取第一轮作为前缀
            prefix = prompt.substr(0, second_user_pos);

            std::cerr << "\n[PrefixExtract] ===== Prefix Extraction =====\n";
            std::cerr << "[PrefixExtract] Chat ID: " << chat_id << '\n';
            std::cerr << "[PrefixExtract] Full prompt length: " << prompt.size() << " bytes\n";
            std::cerr << "[PrefixExtract] Extracted prefix length: " << prefix.size() << " bytes\n";
            std::cerr << "[PrefixExtract] Prefix preview: " << prefix.substr(0, std::min(size_t(100), prefix.size())) << "...\n";

            // 尝试从缓存中查找
            prefix_kv_len = findPrefixCache(prefix);

            if (prefix_kv_len > 0) {
                std::cerr << "[PrefixExtract] ✓ CACHE HIT! Skipping " << prefix_kv_len << " tokens\n";
            } else {
                std::cerr << "[PrefixExtract] ✗ CACHE MISS, will save after inference\n";
            }
            std::cerr << "[PrefixExtract] =============================\n\n";
        } else {
            std::cerr << "[PrefixExtract] Only 1 round, skipping cache (need ≥2 rounds)\n";
        }
    } else {
        std::cerr << "[PrefixExtract] No assistant reply yet, skipping cache\n";
    }

    // 3. 生成（使用前缀缓存）
    std::string output = raw_infer(prompt, maxTokens, temperature, prefix_kv_len);

    // 4. 如果是第一次遇到这个前缀，保存到缓存
    if (!prefix.empty() && prefix_kv_len == 0) {
        // 计算前缀的token数量（需要tokenize）
        const llama_vocab* vocab = llama_model_get_vocab(model_);
        std::vector<llama_token> tokBuf(prefix.size() * 4);
        int nTok = llama_tokenize(
            vocab, prefix.c_str(), (int)prefix.size(),
            tokBuf.data(), (int)tokBuf.size(),
            true, false
        );
        if (nTok > 0) {
            savePrefixCache(prefix, nTok);
        }
    }

    // 4. 截断到下一个 <|user|> 或 <|endoftext|>
    size_t stop_angle = output.find('<');
    if (stop_angle != std::string::npos) output.resize(stop_angle);

    // 去除结尾多余换行
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    // 5. 修复 UTF-8 截断：如果最后一个字节是不完整的 UTF-8 字符，删除它
    while (!output.empty()) {
        unsigned char last = static_cast<unsigned char>(output.back());
        // 如果是 UTF-8 续字节 (10xxxxxx)，说明被截断了，删除
        if ((last & 0xC0) == 0x80) {
            output.pop_back();
        }
        // 如果是多字节序列开头 (11xxxxxx)，检查是否完整
        else if ((last & 0x80) == 0x80) {
            // 简单处理：如果是多字节开头但后面没有字节了，删除
            int expectedBytes = 0;
            if ((last & 0xE0) == 0xC0) expectedBytes = 2;  // 110xxxxx
            else if ((last & 0xF0) == 0xE0) expectedBytes = 3;  // 1110xxxx
            else if ((last & 0xF8) == 0xF0) expectedBytes = 4;  // 11110xxx

            // 检查是否有足够的字节
            int actualBytes = 1;
            for (int i = output.size() - 2; i >= 0 && actualBytes < expectedBytes; --i) {
                if (((unsigned char)output[i] & 0xC0) == 0x80) actualBytes++;
                else break;
            }

            if (actualBytes < expectedBytes) {
                // 不完整，删除这些字节
                output.resize(output.size() - actualBytes);
            }
            break;
        } else {
            // ASCII 字符，完整的
            break;
        }
    }

    // 6. 保存 assistant 回复
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        chat_sessions_[chat_id].add("assistant", output);
    }

    return output;
}
// =========== 清除历史 ===========
void ModelManager::dropSession(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(chat_mutex_);
    chat_sessions_.erase(chat_id);
}

// =========== 前缀缓存实现 (Prefix Tree) ===========

// Tokenize辅助函数
std::vector<int> ModelManager::tokenize(const std::string& text) const {
    if (!model_) return {};

    const llama_vocab* vocab = llama_model_get_vocab(model_);
    std::vector<llama_token> tokBuf(text.size() * 4);
    int nTok = llama_tokenize(
        vocab, text.c_str(), (int)text.size(),
        tokBuf.data(), (int)tokBuf.size(),
        true, false
    );

    if (nTok < 1) return {};

    std::vector<int> result(nTok);
    for (int i = 0; i < nTok; i++) {
        result[i] = tokBuf[i];
    }
    return result;
}

// 查找前缀缓存 (Prefix Tree版本)
int ModelManager::findPrefixCache(const std::string& prefix) {
    std::lock_guard<std::mutex> g(cache_mutex_);
    total_cache_requests_++;

    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed\n";
        return 0;
    }

    auto [seq_id, matched_len] = prefix_tree_.findLongestPrefix(tokens);

    if (seq_id >= 0 && matched_len > 0) {
        // 判断是完全匹配还是部分匹配
        bool is_partial = (matched_len < static_cast<int>(tokens.size()));

        std::cerr << "[PrefixTree] ✓ " << (is_partial ? "PARTIAL HIT" : "FULL HIT") << "! "
                  << "seq_id=" << seq_id
                  << " matched_tokens=" << matched_len << "/" << tokens.size();

        if (is_partial) {
            std::cerr << " (saved " << (tokens.size() - matched_len)
                      << " tokens, " << (100.0 * matched_len / tokens.size()) << "%)";
        }
        std::cerr << " (cache_size=" << prefix_tree_.size() << ")\n";

        // 使用新的 memory API 复制序列
        if (ctx_) {
            llama_memory_t mem = llama_get_memory(ctx_);
            llama_memory_seq_rm(mem, 0, -1, -1);  // 清空序列 0
            llama_memory_seq_cp(mem, seq_id, 0, 0, matched_len);  // 复制到序列 0
        }

        // 只更新匹配部分的统计信息
        std::vector<int> matched_tokens(tokens.begin(), tokens.begin() + matched_len);
        prefix_tree_.updateHit(matched_tokens);
        total_cache_hits_++;

        double hit_rate = (double)total_cache_hits_ / total_cache_requests_ * 100.0;
        std::cerr << "[PrefixTree] Current hit rate: " << hit_rate << "% ("
                  << total_cache_hits_ << "/" << total_cache_requests_ << ")\n";

        return matched_len;
    }

    std::cerr << "[PrefixTree] ✗ MISS for " << tokens.size() << " tokens"
              << " (cache_size=" << prefix_tree_.size() << ")\n";
    return 0;
}

// 保存前缀到缓存 (Prefix Tree版本)
void ModelManager::savePrefixCache(const std::string& prefix, int kv_length) {
    std::lock_guard<std::mutex> g(cache_mutex_);

    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed, cannot save\n";
        return;
    }

    // LRU 淘汰：如果缓存已满，清理最旧的条目
    if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
        auto evicted_seq_ids = prefix_tree_.evictLRU(MAX_PREFIX_CACHE);

        // 清理对应的 llama.cpp 序列，并回收 seq_id
        if (ctx_ && !evicted_seq_ids.empty()) {
            llama_memory_t mem = llama_get_memory(ctx_);
            for (int sid : evicted_seq_ids) {
                llama_memory_seq_rm(mem, sid, -1, -1);
                releaseSeqId(sid);  // 回收 seq_id
                std::cerr << "[PrefixTree] Cleared and recycled seq_id=" << sid << "\n";
            }
        }
    }

    int seq_id = allocateSeqId();  // 使用回收池分配 seq_id

    // 使用新的 memory API 复制序列
    if (ctx_) {
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_cp(mem, 0, seq_id, 0, kv_length);
    }

    prefix_tree_.insert(tokens, seq_id);

    std::cerr << "[PrefixTree] SAVED " << tokens.size() << " tokens"
              << " kv_len=" << kv_length
              << " seq_id=" << seq_id
              << " (total=" << prefix_tree_.size() << ")\n";
}

// 获取缓存统计 (Prefix Tree版本)
ModelManager::CacheStats ModelManager::getCacheStats() const {
    std::lock_guard<std::mutex> g(cache_mutex_);

    CacheStats stats;
    stats.cache_size = prefix_tree_.size();
    stats.total_hits = total_cache_hits_;
    stats.total_requests = total_cache_requests_;
    stats.hit_rate = (total_cache_requests_ > 0)
                     ? (double)total_cache_hits_ / total_cache_requests_
                     : 0.0;

    return stats;
}

// 清空所有前缀缓存 (Prefix Tree版本)
void ModelManager::clearPrefixCache() {
    std::lock_guard<std::mutex> g(cache_mutex_);

    // 使用新的 memory API 清空所有序列
    if (ctx_) {
        llama_memory_t mem = llama_get_memory(ctx_);
        for (int sid = 1; sid < next_seq_id_; sid++) {
            llama_memory_seq_rm(mem, sid, -1, -1);  // 删除整个序列
        }
    }

    prefix_tree_.clear();
    next_seq_id_ = 1;
    total_cache_hits_ = 0;
    total_cache_requests_ = 0;

    std::cerr << "[PrefixTree] CLEARED all caches using new memory API\n";
}

// 预热缓存：预先计算常用模板
void ModelManager::warmupCache(const std::vector<std::string>& prompts) {
    std::lock_guard<std::mutex> g(mtx_);

    std::cout << "\n🔥 ========== Cache Warmup Started ==========" << std::endl;
    std::cout << "📝 Warming up " << prompts.size() << " templates..." << std::endl;

    int success_count = 0;

    for (size_t i = 0; i < prompts.size(); i++) {
        const auto& prompt = prompts[i];

        std::cout << "\n[" << (i+1) << "/" << prompts.size() << "] Processing template:" << std::endl;
        std::cout << "  📄 Content: " << prompt.substr(0, 60)
                  << (prompt.size() > 60 ? "..." : "") << std::endl;

        // 1. Tokenize
        auto tokens = tokenize(prompt);
        if (tokens.empty()) {
            std::cerr << "  ❌ Tokenize failed, skipping" << std::endl;
            continue;
        }

        std::cout << "  🔢 Tokens: " << tokens.size() << std::endl;

        // 2. 清空序列0的KV cache
        if (ctx_) {
            llama_memory_t mem = llama_get_memory(ctx_);
            llama_memory_clear(mem, true);
        }

        // 3. 计算KV cache（一次前向传播）
        llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
        for (size_t j = 0; j < tokens.size(); j++) {
            batch.token[j]     = tokens[j];
            batch.pos[j]       = j;
            batch.seq_id[j][0] = 0;  // 直接赋值，不要 new（llama_batch_init 已分配内存）
            batch.n_seq_id[j]  = 1;
            batch.logits[j]    = (j == tokens.size() - 1);
        }
        batch.n_tokens = tokens.size();

        if (llama_decode(ctx_, batch) != 0) {
            std::cerr << "  ❌ Decode failed, skipping" << std::endl;
            llama_batch_free(batch);
            continue;
        }

        llama_batch_free(batch);

        // 4. 使用savePrefixCache()保存到缓存
        savePrefixCache(prompt, tokens.size());

        std::cout << "  ✅ Cached: tokens=" << tokens.size() << std::endl;
        success_count++;
    }

    std::cout << "\n🔥 ========== Cache Warmup Complete ==========" << std::endl;
    std::cout << "✅ Successfully cached: " << success_count << "/" << prompts.size() << " templates" << std::endl;
    std::cout << "📊 Total cache entries: " << prefix_tree_.size() << std::endl;
    std::cout << "🔥 ============================================\n" << std::endl;
}
