#include "ModelManager.h"
#include "llama.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// 简化的日志宏
#define LOG_INFO(msg) std::cerr << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cerr << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#define LOG_DEBUG(msg) std::cerr << "[DEBUG] " << msg << std::endl

// ---------- ctor / dtor ----------
ModelManager::ModelManager()  = default;
ModelManager::~ModelManager() {
    if (ctx_) llama_free(ctx_);
    if (model_) llama_free_model(model_);
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
        llama_free_model(model_);
        model_ = nullptr;
    }

    // 加载新模型
    llama_model_params mp = llama_model_default_params();
    model_ = llama_load_model_from_file(path.c_str(), mp);
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
    ctx_ = llama_new_context_with_model(model_, cp);

    if (!ctx_) {
        std::cerr << "[Model] context creation failed\n";
        llama_free_model(model_);
        model_ = nullptr;
        return false;
    }

    std::cout << "[Model] loaded ok: " << path << '\n';
    return true;
}

// =========== 单轮推理（底层，无历史） ===========
std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature, int prefix_kv_len, bool use_speculative) const {
    // 打印 prompt 长度
    std::cerr << "[run] prompt bytes=" << prompt.size() << "  maxTok=" << maxTokens << "  prefix_kv=" << prefix_kv_len << '\n';

    // 0. 使用持久的 context
    if (!ctx_) return "[no_ctx]";

    // ======== 推测式解码路径 ========
    // 条件: 1) 已启用 2) 用户未禁用 3) 生成长度足够
    bool should_use_spec = false;
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        should_use_spec = use_speculative &&
                          speculative_decoder_ &&
                          speculative_decoder_->isLoaded() &&
                          speculative_decoder_->shouldEnableForTask(maxTokens);
    }

    if (should_use_spec) {
        std::cerr << "[run] 🚀 Using SPECULATIVE DECODING\n";
        return raw_infer_speculative(prompt, maxTokens, temperature, prefix_kv_len);
    } else {
        std::cerr << "[run] Using GREEDY DECODING\n";
    }

    // ======== 原有贪婪解码路径 ========

    // 如果没有前缀缓存，清空 KV cache；否则保留前缀部分
    if (prefix_kv_len == 0) {
        llama_kv_cache_clear(ctx_);
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

    // Tokenize前缀
    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed\n";
        return 0;
    }

    // 使用Prefix Tree查找最长匹配
    auto [seq_id, matched_len] = prefix_tree_.findLongestPrefix(tokens);

    if (seq_id >= 0 && matched_len > 0) {
        // 命中！复制KV到seq_id=0（当前推理序列）
        std::cerr << "[PrefixTree] ✓ HIT! "
                  << "seq_id=" << seq_id
                  << " matched_tokens=" << matched_len << "/" << tokens.size()
                  << " (cache_size=" << prefix_tree_.size() << ")\n";

        // 使用llama.cpp的seq复制功能
        llama_kv_cache_seq_rm(ctx_, 0, 0, -1);  // 清空seq_id=0
        llama_kv_cache_seq_cp(ctx_, seq_id, 0, 0, matched_len);

        // 更新统计
        prefix_tree_.updateHit(tokens);
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

    // Tokenize前缀
    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed, cannot save\n";
        return;
    }

    // 检查是否需要淘汰
    if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
        prefix_tree_.evictLRU(MAX_PREFIX_CACHE);
    }

    // 分配新序列ID
    int seq_id = next_seq_id_++;

    // 复制当前KV cache（seq_id=0）到新序列
    llama_kv_cache_seq_cp(ctx_, 0, seq_id, 0, kv_length);

    // 插入Prefix Tree
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

    // 注意：这里我们无法遍历Prefix Tree获取所有seq_id
    // 简化处理：清空整个KV cache（seq_id 1到next_seq_id_）
    for (int sid = 1; sid < next_seq_id_; sid++) {
        llama_kv_cache_seq_rm(ctx_, sid, 0, -1);
    }

    prefix_tree_.clear();
    next_seq_id_ = 1;
    total_cache_hits_ = 0;
    total_cache_requests_ = 0;

    std::cerr << "[PrefixTree] CLEARED all caches\n";
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

        // 2. 计算KV cache（仅一次前向传播）
        llama_kv_cache_seq_rm(ctx_, 0, 0, -1);  // 清空seq_id=0

        llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
        for (size_t j = 0; j < tokens.size(); j++) {
            batch.token[j]     = tokens[j];
            batch.pos[j]       = j;
            batch.seq_id[j][0] = 0;
            batch.n_seq_id[j]  = 1;
            batch.logits[j]    = (j == tokens.size() - 1);  // 只有最后一个token需要logits
        }
        batch.n_tokens = tokens.size();

        if (llama_decode(ctx_, batch) != 0) {
            std::cerr << "  ❌ Decode failed, skipping" << std::endl;
            llama_batch_free(batch);
            continue;
        }
        llama_batch_free(batch);

        // 3. 保存到缓存（复用savePrefixCache逻辑）
        {
            std::lock_guard<std::mutex> cache_lock(cache_mutex_);

            // 检查是否需要淘汰
            if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
                prefix_tree_.evictLRU(MAX_PREFIX_CACHE);
            }

            // 分配新序列ID
            int seq_id = next_seq_id_++;

            // 复制当前KV cache（seq_id=0）到新序列
            llama_kv_cache_seq_cp(ctx_, 0, seq_id, 0, tokens.size());

            // 插入Prefix Tree
            prefix_tree_.insert(tokens, seq_id);

            std::cout << "  ✅ Cached: seq_id=" << seq_id
                      << " tokens=" << tokens.size() << std::endl;
            success_count++;
        }
    }

    std::cout << "\n🔥 ========== Cache Warmup Complete ==========" << std::endl;
    std::cout << "✅ Successfully cached: " << success_count << "/" << prompts.size() << " templates" << std::endl;
    std::cout << "📊 Total cache entries: " << prefix_tree_.size() << std::endl;
    std::cout << "🔥 ============================================\n" << std::endl;
}

// ============================================================
// 推测式解码集成
// ============================================================

bool ModelManager::enableSpeculativeDecoding(
    const std::string& draft_model_path,
    const SpeculativeConfig* config
) {
    std::lock_guard<std::mutex> lock(spec_mutex_);

    if (speculative_decoder_) {
        LOG_WARN("Speculative decoding already enabled, disabling first...");
        speculative_decoder_.reset();
    }

    if (!model_) {
        LOG_ERROR("Verifier model not loaded, cannot enable speculative decoding");
        return false;
    }

    // 使用提供的配置或默认配置
    SpeculativeConfig cfg;
    if (config) {
        cfg = *config;
    } else {
        // 自动检测平台并使用最佳配置
        #ifdef __APPLE__
            cfg = SpeculativeConfig::createForAppleSilicon();
            LOG_INFO("Using Apple Silicon optimized config");
        #else
            cfg = SpeculativeConfig::createForCPUOnly();
            LOG_INFO("Using CPU-only config");
        #endif
    }

    cfg.draft_model_path = draft_model_path;

    // 创建SpeculativeDecoder实例
    speculative_decoder_ = std::make_unique<SpeculativeDecoder>(cfg);

    // 加载draft模型
    if (!speculative_decoder_->loadDraftModel()) {
        LOG_ERROR("Failed to load draft model");
        speculative_decoder_.reset();
        return false;
    }

    // 运行兼容性检查
    auto compat_result = speculative_decoder_->checkCompatibility(model_);
    if (!compat_result.is_compatible) {
        LOG_ERROR("Draft/Verifier compatibility check failed!");
        LOG_ERROR(compat_result.error_message);
        LOG_ERROR(compat_result.getSummary());
        speculative_decoder_.reset();
        return false;
    }

    LOG_INFO("✓ Speculative decoding enabled successfully");
    LOG_INFO(compat_result.getSummary());

    return true;
}

void ModelManager::disableSpeculativeDecoding() {
    std::lock_guard<std::mutex> lock(spec_mutex_);

    if (!speculative_decoder_) {
        LOG_WARN("Speculative decoding not enabled");
        return;
    }

    LOG_INFO("Disabling speculative decoding...");
    LOG_INFO(speculative_decoder_->getStatsString());

    speculative_decoder_.reset();
    LOG_INFO("Speculative decoding disabled");
}

bool ModelManager::isSpeculativeDecodingEnabled() const {
    std::lock_guard<std::mutex> lock(spec_mutex_);
    return speculative_decoder_ && speculative_decoder_->isLoaded();
}

std::string ModelManager::getSpeculativeStats() const {
    std::lock_guard<std::mutex> lock(spec_mutex_);

    if (!speculative_decoder_) {
        return "Speculative decoding not enabled";
    }

    return speculative_decoder_->getStatsString();
}

std::string ModelManager::checkSpeculativeCompatibility() const {
    std::lock_guard<std::mutex> lock(spec_mutex_);

    if (!speculative_decoder_) {
        return "Speculative decoder not initialized";
    }

    if (!model_) {
        return "Verifier model not loaded";
    }

    auto result = speculative_decoder_->checkCompatibility(model_);
    return result.getSummary();
}

// ============================================================
// 推测式解码推理路径
// ============================================================

std::string ModelManager::raw_infer_speculative(
    const std::string& prompt,
    int maxTokens,
    float temperature,
    int prefix_kv_len
) const {
    std::cerr << "[spec] Starting speculative decoding...\n";

    if (!ctx_) return "[no_ctx]";

    // ======== 阶段1: Tokenize prompt ========
    if (prefix_kv_len == 0) {
        llama_kv_cache_clear(ctx_);
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    std::vector<llama_token> tokBuf(prompt.size() * 4);
    int nTok = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.size(),
        tokBuf.data(), (int)tokBuf.size(),
        true, false
    );

    if (nTok < 1) { return "[tok_fail]"; }
    tokBuf.resize(nTok);

    // 转换为std::vector<int>
    std::vector<int> prompt_tokens(tokBuf.begin(), tokBuf.end());

    std::cerr << "[spec] Prompt tokenized: " << nTok << " tokens\n";

    // ======== 阶段2: 处理prompt (跳过缓存的部分) ========
    int start_idx = (prefix_kv_len > 0 && prefix_kv_len < nTok) ? prefix_kv_len : 0;
    int tokens_to_process = nTok - start_idx;

    if (tokens_to_process > 0) {
        std::cerr << "[spec] Processing prompt tokens [" << start_idx << ", " << nTok << ")\n";

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
    }

    // ======== 阶段3: 推测式解码生成 ========
    const int eos = llama_vocab_eos(vocab);

    std::vector<int> generated_tokens;
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        generated_tokens = speculative_decoder_->decode(
            ctx_,
            prompt_tokens,
            maxTokens,
            temperature,
            eos
        );
    }

    // ======== 阶段4: 将tokens转换为文本 ========
    std::string output;
    output.reserve(generated_tokens.size() * 4);

    for (int token : generated_tokens) {
        char piece[256] = {0};
        llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);

        // 停止条件检查
        if (piece[0] == '<') break;

        output += piece;
    }

    std::cerr << "[spec] Generated " << generated_tokens.size() << " tokens, "
              << output.size() << " bytes\n";

    // 打印统计信息
    {
        std::lock_guard<std::mutex> lock(spec_mutex_);
        std::cerr << speculative_decoder_->getStatsString();
    }

    return output;
}
