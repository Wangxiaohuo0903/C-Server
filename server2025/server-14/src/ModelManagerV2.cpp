#include "ModelManagerV2.h"
#include <iostream>
#include <cstring>
#include <algorithm>

ModelManagerV2::ModelManagerV2()
    : model_(nullptr),
      n_ctx_(2048),
      n_threads_(4),
      batch_enabled_(false)
{
}

ModelManagerV2::~ModelManagerV2() {
    // 停止批处理引擎
    if (batch_engine_) {
        batch_engine_->stop();
    }

    // 清理 session pool
    session_pool_.reset();

    // 释放模型
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
}

ModelManagerV2& ModelManagerV2::instance() {
    static ModelManagerV2 inst;
    return inst;
}

bool ModelManagerV2::loadModel(
    const std::string& path,
    int n_ctx,
    int n_threads,
    int max_sessions,
    bool enable_batch,
    int batch_size
) {
    std::lock_guard<std::mutex> lock(model_mutex_);

    // 释放旧模型
    if (model_) {
        session_pool_.reset();
        batch_engine_.reset();
        llama_model_free(model_);
        model_ = nullptr;
    }

    // 加载新模型
    llama_model_params mp = llama_model_default_params();

    // macOS Docker 修复: 禁用 mmap，使用内存加载
    mp.use_mmap = false;  // 禁用 mmap
    mp.use_mlock = false; // 禁用 mlock

    model_ = llama_model_load_from_file(path.c_str(), mp);

    if (!model_) {
        std::cerr << "[ModelManagerV2] Failed to load model: " << path << std::endl;
        return false;
    }

    n_ctx_ = n_ctx;
    n_threads_ = n_threads;
    batch_enabled_ = enable_batch;

    std::cout << "[ModelManagerV2] Model loaded: " << path << std::endl;

    // 初始化 SessionContextPool
    try {
        session_pool_ = std::make_unique<SessionContextPool>(
            model_, max_sessions, n_ctx, n_threads
        );
        std::cout << "[ModelManagerV2] SessionContextPool initialized" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ModelManagerV2] Failed to init SessionContextPool: "
                  << e.what() << std::endl;
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    // 初始化 BatchInferenceEngine（如果启用）
    if (batch_enabled_) {
        try {
            batch_engine_ = std::make_unique<BatchInferenceEngine>(
                model_, n_ctx, n_threads, batch_size
            );
            batch_engine_->start();
            std::cout << "[ModelManagerV2] BatchInferenceEngine started" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[ModelManagerV2] Failed to init BatchInferenceEngine: "
                      << e.what() << std::endl;
            // 批处理失败不影响基本功能
            batch_enabled_ = false;
        }
    }

    return true;
}

std::vector<llama_token> ModelManagerV2::tokenize(
    const std::string& text,
    bool add_bos
) const {
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    // 上限估算
    std::vector<llama_token> tok_buf(text.size() + 16);

    int n_tok = llama_tokenize(
        vocab,
        text.c_str(),
        (int)text.size(),
        tok_buf.data(),
        (int)tok_buf.size(),
        add_bos,
        true // parse_special
    );

    if (n_tok < 0) {
        // 如果buffer太小，tokenize返回负数表示需要的长度
        tok_buf.resize(-n_tok);
        n_tok = llama_tokenize(
            vocab,
            text.c_str(),
            (int)text.size(),
            tok_buf.data(),
            (int)tok_buf.size(),
            add_bos,
            true
        );
    }

    if (n_tok < 0) return {};

    tok_buf.resize(n_tok);
    return tok_buf;
}

std::string ModelManagerV2::detokenize(const std::vector<llama_token>& tokens) const {
    const llama_vocab* vocab = llama_model_get_vocab(model_);
    std::string result;
    result.reserve(tokens.size() * 4);

    for (llama_token token : tokens) {
        char piece[256] = {0};
        // llama_token_to_piece(vocab, token, buf, length, lstrip, special)
        int n = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, false);
        if (n < 0) {
             // Handle error or larger buffer needed
             continue; 
        }
        result += std::string(piece, n);
    }

    return result;
}

std::string ModelManagerV2::inferWithCache(
    const std::string& session_id,
    const std::string& prompt,
    const std::vector<llama_token>& tokens,
    int max_tokens,
    float temperature
) {
    if (!session_pool_) {
        std::cerr << "[ModelManagerV2] SessionContextPool not initialized" << std::endl;
        return "[pool_not_init]";
    }

    // 获取或创建 session context
    auto* session_ctx = session_pool_->getOrCreateSession(session_id);
    if (!session_ctx || !session_ctx->ctx) {
        std::cerr << "[ModelManagerV2] Failed to get session context" << std::endl;
        return "[session_fail]";
    }

    // 增量推理
    bool success = session_pool_->processIncrementalTokens(
        session_id, tokens, prompt
    );

    if (!success) {
        std::cerr << "[ModelManagerV2] Incremental inference failed" << std::endl;
        return "[infer_fail]";
    }

    // 生成新 tokens（传入当前缓存的 token 数量作为起始位置）
    std::string result = generateTokens(
        session_ctx->ctx,
        max_tokens,
        temperature,
        session_ctx->cached_token_count  // 当前 KV 缓存的位置
    );

    // 将生成的 tokens 添加到历史（用于下次缓存复用）
    std::vector<llama_token> generated_tokens = tokenize(result, false);
    std::vector<llama_token> full_tokens = tokens;
    full_tokens.insert(full_tokens.end(), generated_tokens.begin(), generated_tokens.end());

    // 更新 session context 的 token 序列
    session_ctx->tokens = full_tokens;
    session_ctx->cached_token_count = (int)full_tokens.size();

    return result;
}

std::string ModelManagerV2::inferWithCache(
    const std::string& session_id,
    const std::string& prompt,
    int max_tokens,
    float temperature
) {
    // 使用 prompt 进行 tokenize (SessionManager 已处理好格式)
    std::vector<llama_token> tokens = tokenize(prompt, true);
    return inferWithCache(session_id, prompt, tokens, max_tokens, temperature);
}

std::future<std::string> ModelManagerV2::inferBatch(
    const std::string& session_id,
    const std::string& prompt,
    int max_tokens,
    float temperature
) {
    if (!batch_enabled_ || !batch_engine_) {
        // 批处理未启用，降级到同步推理
        std::promise<std::string> promise;
        promise.set_value(inferWithCache(session_id, prompt, max_tokens, temperature));
        return promise.get_future();
    }

    return batch_engine_->submitRequest(session_id, prompt, max_tokens, temperature);
}

std::string ModelManagerV2::generateTokens(
    llama_context* ctx,
    int max_tokens,
    float temperature,
    int n_past
) const {
    const llama_model* model = llama_get_model(ctx);
    const llama_vocab* vocab = llama_model_get_vocab(model);
    const llama_token eos = llama_vocab_eos(vocab);
    const int v_size = llama_vocab_n_tokens(vocab);

    std::string out;
    out.reserve(max_tokens * 4);

    llama_batch b_gen = llama_batch_init(1, 0, 1);

    for (int step = 0; step < max_tokens; ++step) {
        float* logits = llama_get_logits(ctx);
        if (!logits) break;

        // 贪婪采样
        int best = 0;
        float best_v = logits[0];
        for (int v = 1; v < v_size; ++v) {
            if (logits[v] > best_v) {
                best_v = logits[v];
                best = v;
            }
        }

        // 转换为文本
        char piece[256] = {0};
        int n = llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
        if (n < 0) {
            // Handle overflow?
        }

        // 检查停止条件
        if (best == eos) break;
        if (n > 0 && std::strstr(piece, "<|") != nullptr) break;
        if (n > 0 && std::strstr(piece, "###") != nullptr) break;
        if (n > 0 && std::strstr(piece, "User:") != nullptr) break;

        // 停止条件未触发，接受该 token
        out += std::string(piece, n);

        // 继续生成下一个 token
        // llama_batch_clear not needed if we overwrite fields?
        // Actually llama_batch is reused.
        
        b_gen.token[0] = best;
        b_gen.pos[0] = n_past++;
        b_gen.seq_id[0][0] = 0;
        b_gen.n_seq_id[0] = 1;
        b_gen.logits[0] = true;
        b_gen.n_tokens = 1;

        if (llama_decode(ctx, b_gen) != 0) break;
    }

    llama_batch_free(b_gen);

    return out;
}

std::string ModelManagerV2::raw_infer(
    const std::string& prompt,
    int max_tokens,
    float temperature
) const {
    std::cerr << "[ModelManagerV2] Warning: raw_infer is deprecated, use inferWithCache instead"
              << std::endl;

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;
    cp.n_threads = n_threads_;

    llama_context* ctx = llama_init_from_model(model_, cp);
    if (!ctx) return "[ctx_fail]";

    // Tokenize
    std::vector<llama_token> tok_buf = tokenize(prompt, true);
    if (tok_buf.empty()) {
        llama_free(ctx);
        return "[tok_fail]";
    }

    // 推送 prompt
    llama_batch full = llama_batch_init((int)tok_buf.size(), 0, 1);
    for (size_t i = 0; i < tok_buf.size(); ++i) {
        full.token[i] = tok_buf[i];
        full.pos[i] = (int)i;
        full.seq_id[i][0] = 0;
        full.n_seq_id[i] = 1;
        full.logits[i] = (i == tok_buf.size() - 1);
    }
    full.n_tokens = (int)tok_buf.size();

    if (llama_decode(ctx, full) != 0) {
        llama_batch_free(full);
        llama_free(ctx);
        return "[decode_fail]";
    }

    llama_batch_free(full);

    // 生成
    // Start generating from n_past = prompt size
    std::string result = generateTokens(ctx, max_tokens, temperature, (int)tok_buf.size());

    llama_free(ctx);

    return result;
}

void ModelManagerV2::deleteSession(const std::string& session_id) {
    if (session_pool_) {
        session_pool_->deleteSession(session_id);
    }
}

const llama_vocab* ModelManagerV2::getVocab() const {
    return llama_model_get_vocab(model_);
}

ModelManagerV2::Stats ModelManagerV2::getStats() const {
    Stats stats;

    if (session_pool_) {
        stats.session_pool_stats = session_pool_->getStats();
    }

    if (batch_enabled_ && batch_engine_) {
        stats.batch_stats = batch_engine_->getStats();
    }

    stats.batch_enabled = batch_enabled_;

    return stats;
}

std::string ModelManagerV2::generateTokensStreaming(
    llama_context* ctx,
    int max_tokens,
    float temperature,
    int n_past,
    StreamCallback callback
) const {
    const llama_model* model = llama_get_model(ctx);
    const llama_vocab* vocab = llama_model_get_vocab(model);
    const llama_token eos = llama_vocab_eos(vocab);
    const int v_size = llama_vocab_n_tokens(vocab);

    std::string out;
    out.reserve(max_tokens * 4);

    llama_batch b_gen = llama_batch_init(1, 0, 1);

    for (int step = 0; step < max_tokens; ++step) {
        float* logits = llama_get_logits(ctx);
        if (!logits) break;

        // 贪婪采样
        int best = 0;
        float best_v = logits[0];
        for (int v = 1; v < v_size; ++v) {
            if (logits[v] > best_v) {
                best_v = logits[v];
                best = v;
            }
        }

        // 转换为文本
        char piece[256] = {0};
        int n = llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
        if (n < 0) {
            // handle error
            continue;
        }

        std::string token_text(piece, n);

        // 检查停止条件
        if (best == eos) break;
        if (std::strstr(piece, "<|") != nullptr) break;
        if (std::strstr(piece, "###") != nullptr) break;
        if (std::strstr(piece, "Q:") != nullptr) break;

        out += token_text;

        // 【流式输出关键】：每生成一个token立即回调
        if (callback && !callback(token_text)) {
            // 如果回调返回false，停止生成
            break;
        }

        // 继续生成下一个 token
        b_gen.token[0] = best;
        b_gen.pos[0] = n_past++;
        b_gen.seq_id[0][0] = 0;
        b_gen.n_seq_id[0] = 1;
        b_gen.logits[0] = true;
        b_gen.n_tokens = 1;

        if (llama_decode(ctx, b_gen) != 0) break;
    }

    llama_batch_free(b_gen);

    return out;
}

std::string ModelManagerV2::inferWithCacheStreaming(
    const std::string& session_id,
    const std::string& prompt,
    int max_tokens,
    float temperature,
    StreamCallback callback
) {
    if (!session_pool_) {
        std::cerr << "[ModelManagerV2] SessionContextPool not initialized" << std::endl;
        return "[pool_not_init]";
    }

    // 使用 prompt 进行 tokenize (SessionManager 已处理好格式)
    std::vector<llama_token> tokens = tokenize(prompt, true);

    // 获取或创建 session context
    auto* session_ctx = session_pool_->getOrCreateSession(session_id);
    if (!session_ctx || !session_ctx->ctx) {
        std::cerr << "[ModelManagerV2] Failed to get session context" << std::endl;
        return "[session_fail]";
    }

    // 增量推理
    bool success = session_pool_->processIncrementalTokens(
        session_id, tokens, prompt
    );

    if (!success) {
        std::cerr << "[ModelManagerV2] Incremental inference failed" << std::endl;
        return "[infer_fail]";
    }

    // 流式生成新 tokens
    std::string result = generateTokensStreaming(
        session_ctx->ctx,
        max_tokens,
        temperature,
        session_ctx->cached_token_count,
        callback
    );

    // 将生成的 tokens 添加到历史
    std::vector<llama_token> generated_tokens = tokenize(result, false);
    std::vector<llama_token> full_tokens = tokens;
    full_tokens.insert(full_tokens.end(), generated_tokens.begin(), generated_tokens.end());

    // 更新 session context
    session_ctx->tokens = full_tokens;
    session_ctx->cached_token_count = (int)full_tokens.size();

    return result;
}

// Server-14: 应用 chat template（使用模型内置模板）
std::string ModelManagerV2::applyChatTemplate(
    const std::vector<std::pair<std::string, std::string>>& messages,
    bool add_generation_prompt
) const {
    if (!model_) {
        std::cerr << "[ModelManagerV2] Model not loaded\n";
        return "";
    }

    // 转换为 llama_chat_message 格式
    std::vector<llama_chat_message> chat_messages;
    for (const auto& msg : messages) {
        chat_messages.push_back({msg.first.c_str(), msg.second.c_str()});
    }

    // Server-14: 打印调试信息
    std::cout << "[ModelManagerV2] applyChatTemplate called with " << messages.size() << " messages\n";
    for (size_t i = 0; i < messages.size(); ++i) {
        std::cout << "  [" << i << "] role=" << messages[i].first << ", content=" << messages[i].second.substr(0, 50) << "...\n";
    }

    // Server-14: 获取模型的 chat template
    const char* tmpl = llama_model_chat_template(model_, nullptr);
    if (tmpl) {
        std::cout << "[ModelManagerV2] Using model's built-in chat template:\n" << std::string(tmpl).substr(0, 200) << "...\n";
    } else {
        std::cout << "[ModelManagerV2] No chat template found in model, using nullptr\n";
    }

    // 第一次调用：获取需要的缓冲区大小
    int32_t required_size = llama_chat_apply_template(
        tmpl,  // Server-14: 使用模型的 chat template（如果有）
        chat_messages.data(),
        chat_messages.size(),
        add_generation_prompt,
        nullptr,
        0
    );

    if (required_size <= 0) {
        std::cerr << "[ModelManagerV2] Failed to apply chat template (required_size=" << required_size << ")\n";
        return "";
    }

    std::cout << "[ModelManagerV2] Chat template requires " << required_size << " bytes\n";

    // 分配缓冲区并再次调用
    std::vector<char> buffer(required_size + 1);  // +1 for null terminator
    int32_t actual_size = llama_chat_apply_template(
        tmpl,  // Server-14: 使用模型的 chat template（如果有）
        chat_messages.data(),
        chat_messages.size(),
        add_generation_prompt,
        buffer.data(),
        buffer.size()
    );

    if (actual_size < 0) {
        std::cerr << "[ModelManagerV2] Failed to apply chat template on second call (actual_size=" << actual_size << ")\n";
        return "";
    }

    std::string result(buffer.data(), actual_size);
    std::cout << "[ModelManagerV2] Generated prompt (length=" << result.length() << "):\n" << result << "\n===END PROMPT===\n";

    return result;
}