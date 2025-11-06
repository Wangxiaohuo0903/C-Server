#include "SpeculativeDecoder.h"
#include "llama.h"
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <cstring>

// 简化的日志宏 (不依赖Logger.h)
#define LOG_INFO(msg) std::cerr << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cerr << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#define LOG_DEBUG(msg) std::cerr << "[DEBUG] " << msg << std::endl

// ============================================================
// llama_batch辅助函数 (兼容新版llama.cpp API)
// ============================================================

static void batch_add(llama_batch& batch, llama_token token, llama_pos pos,
                      const std::vector<llama_seq_id>& seq_ids, bool logits) {
    batch.token[batch.n_tokens] = token;
    batch.pos[batch.n_tokens] = pos;
    batch.n_seq_id[batch.n_tokens] = seq_ids.size();
    for (size_t i = 0; i < seq_ids.size(); ++i) {
        batch.seq_id[batch.n_tokens][i] = seq_ids[i];
    }
    batch.logits[batch.n_tokens] = logits ? 1 : 0;
    batch.n_tokens++;
}

static void batch_clear(llama_batch& batch) {
    batch.n_tokens = 0;
}

// ============================================================
// 构造函数与析构函数
// ============================================================

SpeculativeDecoder::SpeculativeDecoder(const SpeculativeConfig& config)
    : config_(config)
{
    stats_.current_K.store(config_.draft_tokens_K, std::memory_order_relaxed);
    LOG_INFO("SpeculativeDecoder initialized with K=" + std::to_string(config_.draft_tokens_K));
}

SpeculativeDecoder::~SpeculativeDecoder() {
    unloadDraftModel();
    LOG_INFO("SpeculativeDecoder destroyed");
}

// ============================================================
// 模型加载/卸载
// ============================================================

bool SpeculativeDecoder::loadDraftModel() {
    std::lock_guard<std::mutex> lock(draft_mutex_);

    if (draft_model_ != nullptr) {
        LOG_WARN("Draft model already loaded, skipping...");
        return true;
    }

    if (config_.draft_model_path.empty()) {
        LOG_ERROR("Draft model path is empty");
        return false;
    }

    LOG_INFO("Loading draft model from: " + config_.draft_model_path);

    // 设置模型参数
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config_.draft_n_gpu_layers;

    // 加载模型
    draft_model_ = llama_load_model_from_file(
        config_.draft_model_path.c_str(),
        model_params
    );

    if (!draft_model_) {
        LOG_ERROR("Failed to load draft model from: " + config_.draft_model_path);
        return false;
    }

    // 创建context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.draft_n_ctx;
    ctx_params.n_threads = config_.draft_n_threads;
    ctx_params.n_threads_batch = config_.draft_n_threads;

    draft_ctx_ = llama_new_context_with_model(draft_model_, ctx_params);

    if (!draft_ctx_) {
        LOG_ERROR("Failed to create draft context");
        llama_free_model(draft_model_);
        draft_model_ = nullptr;
        return false;
    }

    LOG_INFO("Draft model loaded successfully");
    LOG_INFO("  - Context size: " + std::to_string(config_.draft_n_ctx));
    LOG_INFO("  - CPU threads: " + std::to_string(config_.draft_n_threads));
    LOG_INFO("  - GPU layers: " + std::to_string(config_.draft_n_gpu_layers));

    return true;
}

void SpeculativeDecoder::unloadDraftModel() {
    std::lock_guard<std::mutex> lock(draft_mutex_);

    if (draft_ctx_) {
        llama_free(draft_ctx_);
        draft_ctx_ = nullptr;
    }

    if (draft_model_) {
        llama_free_model(draft_model_);
        draft_model_ = nullptr;
    }

    LOG_INFO("Draft model unloaded");
}

// ============================================================
// 配置验证 - 规避常见坑点
// ============================================================

SpeculativeDecoder::CompatibilityResult SpeculativeDecoder::checkCompatibility(
    llama_model* verifier_model
) const {
    CompatibilityResult result;

    if (!draft_model_ || !verifier_model) {
        result.is_compatible = false;
        result.error_message = "Draft or verifier model is null";
        return result;
    }

    // ======== 坑点1: 词表大小必须一致 ========
    const llama_vocab* vocab_draft = llama_model_get_vocab(draft_model_);
    const llama_vocab* vocab_verifier = llama_model_get_vocab(verifier_model);

    int vocab_size_draft = llama_vocab_n_tokens(vocab_draft);
    int vocab_size_verifier = llama_vocab_n_tokens(vocab_verifier);

    result.vocab_size_draft = vocab_size_draft;
    result.vocab_size_verifier = vocab_size_verifier;

    if (vocab_size_draft != vocab_size_verifier) {
        result.is_compatible = false;
        result.vocab_match = false;
        result.error_message = "Vocabulary size mismatch: draft=" +
                               std::to_string(vocab_size_draft) +
                               " vs verifier=" + std::to_string(vocab_size_verifier) +
                               " (CRITICAL: token alignment will be wrong!)";
        LOG_ERROR(result.error_message);
        return result;
    }

    result.vocab_match = true;

    // ======== 坑点2: 分词器类型必须一致 ========
    // 检查特殊token是否匹配 (BOS, EOS, etc.)
    int bos_draft = llama_vocab_bos(vocab_draft);
    int bos_verifier = llama_vocab_bos(vocab_verifier);
    int eos_draft = llama_vocab_eos(vocab_draft);
    int eos_verifier = llama_vocab_eos(vocab_verifier);

    if (bos_draft != bos_verifier || eos_draft != eos_verifier) {
        result.is_compatible = false;
        result.tokenizer_match = false;
        result.error_message = "Special tokens mismatch: " \
                               "BOS(draft=" + std::to_string(bos_draft) +
                               " vs verifier=" + std::to_string(bos_verifier) + "), " +
                               "EOS(draft=" + std::to_string(eos_draft) +
                               " vs verifier=" + std::to_string(eos_verifier) + ")";
        LOG_ERROR(result.error_message);
        return result;
    }

    result.tokenizer_match = true;

    // ======== 坑点3: 验证几个常见token的编码一致性 ========
    const char* test_tokens[] = {" hello", " world", "\n", "the"};
    for (const char* text : test_tokens) {
        std::vector<llama_token> draft_encoded(10);
        std::vector<llama_token> verifier_encoded(10);

        int n_draft = llama_tokenize(vocab_draft, text, strlen(text),
                                      draft_encoded.data(), draft_encoded.size(),
                                      false, false);
        int n_verifier = llama_tokenize(vocab_verifier, text, strlen(text),
                                        verifier_encoded.data(), verifier_encoded.size(),
                                        false, false);

        if (n_draft != n_verifier) {
            result.is_compatible = false;
            result.tokenizer_match = false;
            result.error_message = "Tokenizer encoding mismatch for \"" +
                                   std::string(text) + "\": draft produced " +
                                   std::to_string(n_draft) + " tokens vs verifier " +
                                   std::to_string(n_verifier) + " tokens";
            LOG_ERROR(result.error_message);
            return result;
        }

        // 检查token序列是否完全一致
        for (int i = 0; i < n_draft; ++i) {
            if (draft_encoded[i] != verifier_encoded[i]) {
                result.is_compatible = false;
                result.tokenizer_match = false;
                result.error_message = "Token ID mismatch at position " +
                                       std::to_string(i) + " for \"" + text + "\"";
                LOG_ERROR(result.error_message);
                return result;
            }
        }
    }

    result.tokenizer_match = true;
    result.is_compatible = true;
    result.error_message = "Models are compatible!";

    LOG_INFO("✓ Compatibility check passed:");
    LOG_INFO("  - Vocabulary size: " + std::to_string(vocab_size_draft));
    LOG_INFO("  - Tokenizer: compatible");
    LOG_INFO("  - Special tokens: BOS=" + std::to_string(bos_draft) +
                 ", EOS=" + std::to_string(eos_draft));

    return result;
}

std::string SpeculativeDecoder::CompatibilityResult::getSummary() const {
    std::ostringstream oss;
    oss << "\n=== Drafter/Verifier Compatibility ===\n";
    oss << "Overall: " << (is_compatible ? "✓ COMPATIBLE" : "✗ INCOMPATIBLE") << "\n";
    oss << "Vocab match: " << (vocab_match ? "✓" : "✗")
        << " (draft=" << vocab_size_draft
        << ", verifier=" << vocab_size_verifier << ")\n";
    oss << "Tokenizer match: " << (tokenizer_match ? "✓" : "✗") << "\n";
    if (!is_compatible) {
        oss << "Error: " << error_message << "\n";
    }
    oss << "======================================\n";
    return oss.str();
}

// ============================================================
// 核心解码方法
// ============================================================

std::vector<int> SpeculativeDecoder::decode(
    llama_context* verifier_ctx,
    const std::vector<int>& prompt_tokens,
    int max_tokens,
    float temperature,
    int stop_token
) {
    if (!isLoaded()) {
        LOG_ERROR("Draft model not loaded, cannot perform speculative decoding");
        return {};
    }

    if (!verifier_ctx) {
        LOG_ERROR("Verifier context is null");
        return {};
    }

    // ======== 坑点4: 极短回答不建议使用推测式解码 ========
    if (max_tokens < 10) {
        LOG_WARN("⚠ Very short generation (max_tokens=" + std::to_string(max_tokens) +
                     "), speculative decoding may be slower than greedy. Consider disabling.");
    }

    // ======== 坑点5: 温度过高可能导致接受率过低 ========
    if (temperature > 0.9f) {
        LOG_WARN("⚠ High temperature (" + std::to_string(temperature) +
                     ") may cause low acceptance rate (<30%). Consider lowering temperature.");
    }

    std::vector<int> generated_tokens;
    std::vector<int> current_context = prompt_tokens;

    LOG_DEBUG("Starting speculative decoding (max_tokens=" + std::to_string(max_tokens) + ")");

    int step = 0;
    int K = stats_.current_K.load(std::memory_order_relaxed);

    while ((int)generated_tokens.size() < max_tokens) {
        // ======== 第1阶段: 起草K个tokens ========
        ScopedTimer draft_timer(stats_.total_draft_time_ns);
        std::vector<int> draft_tokens = draftTokens(current_context, K, temperature);
        draft_timer.~ScopedTimer();  // 手动结束计时

        if (draft_tokens.empty()) {
            LOG_WARN("Drafter returned no tokens, stopping");
            break;
        }

        LOG_DEBUG("Step " + std::to_string(step) + ": drafted " +
                     std::to_string(draft_tokens.size()) + " tokens");

        // ======== 第2阶段: 批量校验tokens ========
        ScopedTimer verify_timer(stats_.total_verify_time_ns);
        VerifyResult verify_result = verifyTokensBatch(
            verifier_ctx,
            current_context,
            draft_tokens,
            temperature
        );
        verify_timer.~ScopedTimer();

        LOG_DEBUG("Step " + std::to_string(step) + ": accepted " +
                     std::to_string(verify_result.accepted_count) + "/" +
                     std::to_string(draft_tokens.size()) + " draft tokens");

        // ======== 第3阶段: 更新统计 & 上下文 ========
        stats_.total_drafted.fetch_add(draft_tokens.size(), std::memory_order_relaxed);
        stats_.total_accepted.fetch_add(verify_result.accepted_tokens.size(), std::memory_order_relaxed);
        stats_.total_steps.fetch_add(1, std::memory_order_relaxed);

        // 🔥 新算法: 更新滑动窗口接受率 (每步都更新)
        float step_acceptance_rate = static_cast<float>(verify_result.accepted_count) / draft_tokens.size();
        stats_.updateRecentAcceptanceRate(step_acceptance_rate);

        // 添加接受的tokens到结果
        for (int token : verify_result.accepted_tokens) {
            generated_tokens.push_back(token);
            current_context.push_back(token);

            // 检查是否遇到停止token
            if (token == stop_token) {
                LOG_DEBUG("Encountered stop token, ending generation");
                goto finish;
            }

            // 检查是否达到最大长度
            if ((int)generated_tokens.size() >= max_tokens) {
                goto finish;
            }
        }

        // 如果没有接受任何token,说明有问题,停止
        if (verify_result.accepted_tokens.empty()) {
            LOG_WARN("No tokens accepted, stopping generation");
            break;
        }

        // 🔥 新算法: Early Stopping检查 (每5步检查一次)
        if (step > 0 && step % 5 == 0) {
            if (shouldFallbackToGreedy()) {
                LOG_WARN("Early stopping triggered - switching to greedy decoding would be faster");
                LOG_WARN("Recommendation: Disable speculative decoding for this task");
                // 注意: 这里只是警告,不实际切换 (因为需要重新初始化context)
                // 实际部署时可以在ModelManager层实现真正的切换
            }
        }

        // 🔥 新算法: 智能K值调整 (使用温度自适应)
        if (config_.enable_dynamic_K && step > 0) {
            // 每3步调整一次 (比原来的5步更激进)
            if (step % 3 == 0) {
                smartAdjustK(temperature);
                K = stats_.current_K.load(std::memory_order_relaxed);
            }
        }

        // ======== 坑点6: 接受率过低预警 ========
        if (step > 5 && step % 10 == 0) {
            float current_rate = stats_.getAcceptanceRate();
            if (current_rate < 0.30f) {
                LOG_WARN("⚠ Low acceptance rate detected: " +
                             std::to_string(current_rate * 100) + "% (< 30%)");
                LOG_WARN("  Possible causes:");
                LOG_WARN("  1. Draft model too weak for this task");
                LOG_WARN("  2. Temperature too high");
                LOG_WARN("  3. Different model architectures");
                LOG_WARN("  → Speculative decoding may be SLOWER than greedy!");
            }
        }

        step++;
    }

finish:
    float final_acceptance_rate = stats_.getAcceptanceRate();
    float final_speedup = stats_.getSpeedup();

    LOG_INFO("Speculative decoding finished: " + std::to_string(generated_tokens.size()) +
                 " tokens in " + std::to_string(step) + " steps");
    LOG_INFO("  Acceptance rate: " + std::to_string(final_acceptance_rate * 100) + "%");
    LOG_INFO("  Speedup: " + std::to_string(final_speedup) + "x");

    // ======== 最终性能评估 ========
    if (final_speedup < 1.0f) {
        LOG_WARN("⚠ Speculative decoding was SLOWER than greedy (" +
                     std::to_string(final_speedup) + "x)!");
        LOG_WARN("  Consider disabling for this use case.");
    } else if (final_speedup > 2.0f) {
        LOG_INFO("✓ Excellent speedup! Speculative decoding is working well.");
    }

    return generated_tokens;
}

// ============================================================
// 起草阶段 (使用小模型)
// ============================================================

std::vector<int> SpeculativeDecoder::draftTokens(
    const std::vector<int>& input_tokens,
    int K,
    float temperature
) {
    std::lock_guard<std::mutex> lock(draft_mutex_);

    if (!draft_ctx_) {
        LOG_ERROR("Draft context is null");
        return {};
    }

    std::vector<int> drafted;
    drafted.reserve(K);

    // 清空draft context的KV cache (使用独立序列)
    llama_kv_cache_clear(draft_ctx_);

    // 处理输入tokens (prompt + 已生成的tokens)
    llama_batch batch = llama_batch_init(input_tokens.size() + K, 0, 1);

    // 先处理所有输入tokens (prefill阶段)
    for (size_t i = 0; i < input_tokens.size(); ++i) {
        batch_add(batch, input_tokens[i], i, {0}, false);
    }

    // 最后一个token需要输出logits
    if (batch.n_tokens > 0) {
        batch.logits[batch.n_tokens - 1] = true;
    }

    // Prefill阶段
    if (llama_decode(draft_ctx_, batch) != 0) {
        LOG_ERROR("Draft prefill failed");
        llama_batch_free(batch);
        return {};
    }

    // 自回归生成K个tokens
    int n_cur = input_tokens.size();
    for (int i = 0; i < K; ++i) {
        // 采样下一个token
        int next_token = sampleToken(draft_ctx_, temperature);

        drafted.push_back(next_token);

        // 准备下一次decode
        batch_clear(batch);
        batch_add(batch, next_token, n_cur, {0}, true);

        if (llama_decode(draft_ctx_, batch) != 0) {
            LOG_WARN("Draft decode failed at token " + std::to_string(i));
            break;
        }

        n_cur++;
    }

    llama_batch_free(batch);
    return drafted;
}

// ============================================================
// 校验阶段 (使用大模型批量校验)
// ============================================================

SpeculativeDecoder::VerifyResult SpeculativeDecoder::verifyTokensBatch(
    llama_context* verifier_ctx,
    const std::vector<int>& input_tokens,
    const std::vector<int>& draft_tokens,
    float temperature
) {
    VerifyResult result;
    result.accepted_count = 0;

    if (draft_tokens.empty()) {
        return result;
    }

    // ============================================================
    // 推测式解码核心: 批量校验draft tokens
    // ============================================================
    // 原理:
    // 1. Verifier一次性处理所有K个draft tokens (批量prefill)
    // 2. 对比每个位置上verifier和drafter的概率分布
    // 3. 从左到右接受tokens,直到遇到第一个拒绝的token
    // 4. 如果所有draft tokens都被接受,verifier额外生成1个token
    // ============================================================

    llama_batch batch = llama_batch_init(draft_tokens.size() + 1, 0, 1);

    // ======== 阶段1: 批量处理所有draft tokens ========
    // 构造batch: 包含所有draft tokens
    int n_cur = input_tokens.size();  // 当前位置(在输入序列之后)

    for (size_t i = 0; i < draft_tokens.size(); ++i) {
        // 添加draft token到batch
        // logits=true 表示我们需要获取这个位置的输出概率分布
        batch_add(batch, draft_tokens[i], n_cur + i, {0}, true);
    }

    // 执行批量推理 (单次forward pass处理所有K个tokens)
    if (llama_decode(verifier_ctx, batch) != 0) {
        LOG_ERROR("Verifier batch decode failed");
        llama_batch_free(batch);
        return result;
    }

    // ======== 阶段2: 逐个校验draft tokens ========
    bool all_accepted = true;

    for (size_t i = 0; i < draft_tokens.size(); ++i) {
        // 获取verifier在位置i的logits
        float* verifier_logits = llama_get_logits_ith(verifier_ctx, i);
        if (!verifier_logits) {
            LOG_ERROR("Failed to get verifier logits at position " + std::to_string(i));
            all_accepted = false;
            break;
        }

        int draft_token = draft_tokens[i];
        const llama_vocab* vocab = llama_model_get_vocab(llama_get_model(verifier_ctx));
        int n_vocab = llama_vocab_n_tokens(vocab);

        // 方法1: 贪婪校验 (简化版)
        // 如果temperature很低,使用贪婪采样
        if (temperature < 0.01f) {
            // 找到verifier的top-1 token
            int verifier_top_token = 0;
            float max_logit = verifier_logits[0];
            for (int t = 1; t < n_vocab; ++t) {
                if (verifier_logits[t] > max_logit) {
                    max_logit = verifier_logits[t];
                    verifier_top_token = t;
                }
            }

            // 检查draft token是否匹配
            if (draft_token == verifier_top_token) {
                result.accepted_tokens.push_back(draft_token);
                result.accepted_count++;
            } else {
                // 拒绝: 使用verifier的token替代
                result.accepted_tokens.push_back(verifier_top_token);
                result.accepted_count++;
                all_accepted = false;
                break;  // 停止校验后续tokens
            }
        } else {
            // 方法2: 概率匹配校验 (推荐)
            // 计算draft token在verifier分布中的概率
            float draft_token_prob = getTokenProbability(verifier_ctx, draft_token, i);

            // 接受条件: draft token概率足够高
            // 阈值可配置 (config_.rejection_threshold)
            if (draft_token_prob > config_.rejection_threshold) {
                result.accepted_tokens.push_back(draft_token);
                result.accepted_count++;
            } else {
                // 拒绝: 从verifier的分布中重新采样
                int resampled_token = sampleToken(verifier_ctx, temperature);
                result.accepted_tokens.push_back(resampled_token);
                result.accepted_count++;
                all_accepted = false;
                break;
            }
        }
    }

    // ======== 阶段3: 如果所有draft tokens都被接受,verifier额外生成1个token ========
    if (all_accepted && result.accepted_count == (int)draft_tokens.size()) {
        // 准备生成第K+1个token
        batch_clear(batch);
        int last_draft_token = draft_tokens.back();
        batch_add(batch, last_draft_token, n_cur + draft_tokens.size() - 1, {0}, true);

        if (llama_decode(verifier_ctx, batch) == 0) {
            // 从verifier采样额外的token
            int bonus_token = sampleToken(verifier_ctx, temperature);
            result.accepted_tokens.push_back(bonus_token);
            result.accepted_count++;
            LOG_DEBUG("All draft tokens accepted + 1 bonus token from verifier");
        }
    }

    llama_batch_free(batch);

    LOG_DEBUG("Batch verification: accepted " +
                 std::to_string(result.accepted_count) + "/" +
                 std::to_string(draft_tokens.size()) + " draft tokens");

    return result;
}

// ============================================================
// 辅助方法
// ============================================================

int SpeculativeDecoder::sampleToken(llama_context* ctx, float temperature) const {
    if (!ctx) return -1;

    // 获取logits
    float* logits = llama_get_logits_ith(ctx, -1);
    if (!logits) return -1;

    const llama_vocab* vocab = llama_model_get_vocab(llama_get_model(ctx));
    int n_vocab = llama_vocab_n_tokens(vocab);

    // 简单的温度采样 (贪婪采样如果temperature接近0)
    if (temperature < 0.01f) {
        // 贪婪采样: 找最大概率的token
        int max_idx = 0;
        float max_logit = logits[0];
        for (int i = 1; i < n_vocab; ++i) {
            if (logits[i] > max_logit) {
                max_logit = logits[i];
                max_idx = i;
            }
        }
        return max_idx;
    }

    // TODO: 实现完整的温度采样
    // 当前简化版: 贪婪采样
    int max_idx = 0;
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        if (logits[i] > max_logit) {
            max_logit = logits[i];
            max_idx = i;
        }
    }
    return max_idx;
}

float SpeculativeDecoder::getTokenProbability(llama_context* ctx, int token, int pos) const {
    if (!ctx) return 0.0f;

    float* logits = llama_get_logits_ith(ctx, pos);
    if (!logits) return 0.0f;

    const llama_vocab* vocab = llama_model_get_vocab(llama_get_model(ctx));
    int n_vocab = llama_vocab_n_tokens(vocab);
    if (token < 0 || token >= n_vocab) return 0.0f;

    // 计算softmax概率
    // TODO: 优化 - 可以只计算需要的token概率
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        if (logits[i] > max_logit) max_logit = logits[i];
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += std::exp(logits[i] - max_logit);
    }

    float prob = std::exp(logits[token] - max_logit) / sum_exp;
    return prob;
}

void SpeculativeDecoder::adjustKValue(float current_acceptance_rate) {
    int current_K = stats_.current_K.load(std::memory_order_relaxed);

    if (current_acceptance_rate < config_.target_acceptance_rate - 0.1f) {
        // 接受率过低,减小K
        stats_.consecutive_low_accept.fetch_add(1, std::memory_order_relaxed);
        stats_.consecutive_high_accept.store(0, std::memory_order_relaxed);

        if (stats_.consecutive_low_accept.load() >= 3 && current_K > config_.min_K) {
            int new_K = std::max(config_.min_K, current_K - 1);
            stats_.current_K.store(new_K, std::memory_order_relaxed);
            stats_.consecutive_low_accept.store(0, std::memory_order_relaxed);
            LOG_INFO("Reduced K from " + std::to_string(current_K) + " to " + std::to_string(new_K));
        }
    } else if (current_acceptance_rate > config_.target_acceptance_rate + 0.1f) {
        // 接受率过高,增大K
        stats_.consecutive_high_accept.fetch_add(1, std::memory_order_relaxed);
        stats_.consecutive_low_accept.store(0, std::memory_order_relaxed);

        if (stats_.consecutive_high_accept.load() >= 3 && current_K < config_.max_K) {
            int new_K = std::min(config_.max_K, current_K + 1);
            stats_.current_K.store(new_K, std::memory_order_relaxed);
            stats_.consecutive_high_accept.store(0, std::memory_order_relaxed);
            LOG_INFO("Increased K from " + std::to_string(current_K) + " to " + std::to_string(new_K));
        }
    }
}

// ============================================================
// 智能K值调整 (算法优化核心)
// ============================================================
void SpeculativeDecoder::smartAdjustK(float temperature) {
    // 获取近期接受率 (滑动窗口)
    float recent_rate = stats_.getRecentAcceptanceRate();
    int current_K = stats_.current_K.load(std::memory_order_relaxed);

    // 算法1: 温度自适应调整
    // 高温度(>0.7)会降低接受率,应使用更小的K
    // 低温度(<0.3)会提高接受率,可以使用更大的K
    float temp_factor = 1.0f;
    if (temperature > 0.7f) {
        temp_factor = 0.7f;  // 高温度惩罚,倾向减小K
    } else if (temperature < 0.3f) {
        temp_factor = 1.3f;  // 低温度奖励,倾向增大K
    }

    // 算法2: 基于近期趋势的激进调整
    // 如果近期接受率持续低于20%,说明drafter太弱,快速降低K
    if (recent_rate < 0.2f && current_K > config_.min_K) {
        int new_K = std::max(config_.min_K, current_K - 2);  // 一次降2个
        stats_.current_K.store(new_K, std::memory_order_relaxed);
        stats_.consecutive_low_accept.store(0, std::memory_order_relaxed);
        LOG_WARN("Critical low acceptance (" + std::to_string(static_cast<int>(recent_rate * 100)) +
                 "%), aggressively reduced K: " + std::to_string(current_K) + " -> " + std::to_string(new_K));
        return;
    }

    // 算法3: 基于效率的动态调整
    // 计算当前配置的实际加速比
    float current_speedup = stats_.getSpeedup();
    uint64_t steps = stats_.total_steps.load(std::memory_order_relaxed);

    if (steps >= 5) {  // 至少5步后才开始调整
        // 如果加速比 < 1.2x,说明推测式解码效果不佳
        if (current_speedup < 1.2f) {
            // 尝试减小K提高接受率
            if (current_K > config_.min_K) {
                int new_K = std::max(config_.min_K, static_cast<int>(current_K * 0.8f));
                stats_.current_K.store(new_K, std::memory_order_relaxed);
                LOG_INFO("Low speedup (" + std::to_string(current_speedup) +
                         "x), reduced K: " + std::to_string(current_K) + " -> " + std::to_string(new_K));
                return;
            }
        }
        // 如果加速比 > 2.0x 且接受率 > 60%,可以尝试增大K
        else if (current_speedup > 2.0f && recent_rate > 0.6f && current_K < config_.max_K) {
            int new_K = std::min(config_.max_K, current_K + 1);
            stats_.current_K.store(new_K, std::memory_order_relaxed);
            LOG_INFO("High speedup (" + std::to_string(current_speedup) +
                     "x), increased K: " + std::to_string(current_K) + " -> " + std::to_string(new_K));
            return;
        }
    }

    // 算法4: 结合温度因子的常规调整
    float adjusted_target = config_.target_acceptance_rate * temp_factor;

    if (recent_rate < adjusted_target - 0.15f) {
        // 接受率明显低于目标
        stats_.consecutive_low_accept.fetch_add(1, std::memory_order_relaxed);
        stats_.consecutive_high_accept.store(0, std::memory_order_relaxed);

        if (stats_.consecutive_low_accept.load() >= 2 && current_K > config_.min_K) {
            int new_K = std::max(config_.min_K, current_K - 1);
            stats_.current_K.store(new_K, std::memory_order_relaxed);
            stats_.consecutive_low_accept.store(0, std::memory_order_relaxed);
            LOG_INFO("Smart adjust (temp=" + std::to_string(temperature) +
                     ", rate=" + std::to_string(static_cast<int>(recent_rate * 100)) +
                     "%), reduced K: " + std::to_string(current_K) + " -> " + std::to_string(new_K));
        }
    } else if (recent_rate > adjusted_target + 0.15f) {
        // 接受率明显高于目标
        stats_.consecutive_high_accept.fetch_add(1, std::memory_order_relaxed);
        stats_.consecutive_low_accept.store(0, std::memory_order_relaxed);

        if (stats_.consecutive_high_accept.load() >= 2 && current_K < config_.max_K) {
            int new_K = std::min(config_.max_K, current_K + 1);
            stats_.current_K.store(new_K, std::memory_order_relaxed);
            stats_.consecutive_high_accept.store(0, std::memory_order_relaxed);
            LOG_INFO("Smart adjust (temp=" + std::to_string(temperature) +
                     ", rate=" + std::to_string(static_cast<int>(recent_rate * 100)) +
                     "%), increased K: " + std::to_string(current_K) + " -> " + std::to_string(new_K));
        }
    }
}

// ============================================================
// Early Stopping机制 (算法优化核心)
// ============================================================
bool SpeculativeDecoder::shouldFallbackToGreedy() const {
    uint64_t steps = stats_.total_steps.load(std::memory_order_relaxed);

    // 至少运行10步后才判断
    if (steps < 10) {
        return false;
    }

    // 条件1: 如果近期接受率持续极低 (<15%),不如直接用贪婪解码
    float recent_rate = stats_.getRecentAcceptanceRate();
    if (recent_rate < 0.15f) {
        LOG_WARN("Early stopping triggered: acceptance rate too low (" +
                 std::to_string(static_cast<int>(recent_rate * 100)) + "%)");
        return true;
    }

    // 条件2: 如果实际加速比 < 1.0x (反而变慢),立即切换
    float speedup = stats_.getSpeedup();
    if (speedup < 1.0f) {
        LOG_WARN("Early stopping triggered: negative speedup (" + std::to_string(speedup) + "x)");
        return true;
    }

    // 条件3: 如果draft时间远超verify时间 (>5x),说明drafter太慢
    double avg_draft = stats_.getAvgDraftTimeMs();
    double avg_verify = stats_.getAvgVerifyTimeMs();
    if (avg_verify > 0 && avg_draft / avg_verify > 5.0) {
        LOG_WARN("Early stopping triggered: draft too slow (draft/verify ratio: " +
                 std::to_string(avg_draft / avg_verify) + ")");
        return true;
    }

    return false;
}

// ============================================================
// 统计信息
// ============================================================

std::string SpeculativeDecoder::getStatsString() const {
    std::ostringstream oss;
    oss << "\n=== Speculative Decoding Statistics ===\n";
    oss << "Total steps:        " << stats_.total_steps.load() << "\n";
    oss << "Total drafted:      " << stats_.total_drafted.load() << " tokens\n";
    oss << "Total accepted:     " << stats_.total_accepted.load() << " tokens\n";
    oss << "Acceptance rate:    " << (stats_.getAcceptanceRate() * 100) << "%\n";
    oss << "Speedup:            " << stats_.getSpeedup() << "x\n";
    oss << "Avg draft time:     " << stats_.getAvgDraftTimeMs() << " ms\n";
    oss << "Avg verify time:    " << stats_.getAvgVerifyTimeMs() << " ms\n";
    oss << "Current K:          " << stats_.current_K.load() << "\n";
    oss << "========================================\n";
    return oss.str();
}

void SpeculativeDecoder::updateConfig(const SpeculativeConfig& new_config) {
    // 只允许更新部分运行时参数
    config_.draft_tokens_K = new_config.draft_tokens_K;
    config_.rejection_threshold = new_config.rejection_threshold;
    config_.enable_dynamic_K = new_config.enable_dynamic_K;
    config_.target_acceptance_rate = new_config.target_acceptance_rate;

    stats_.current_K.store(new_config.draft_tokens_K, std::memory_order_relaxed);

    LOG_INFO("Config updated: K=" + std::to_string(new_config.draft_tokens_K) +
                 ", dynamic_K=" + (new_config.enable_dynamic_K ? "true" : "false"));
}
