#include "SpeculativeDecoder.h"
#include "TaskClassifier.h"
#include "ConfidenceGuide.h"
#include "llama.h"
#include <iostream>
#include <iomanip>   // for std::setprecision
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdlib>  // for rand(), srand()
#include <ctime>    // for time()
#include <vector>   // for std::vector
#include <numeric>  // for std::accumulate

// ============================================================
// 构造与析构
// ============================================================

SpeculativeDecoder::SpeculativeDecoder(
    llama_model* model_target,
    llama_context* ctx_target,
    const std::string& draft_model_path
) : SpeculativeDecoder(model_target, ctx_target, draft_model_path, Config())
{
}

SpeculativeDecoder::SpeculativeDecoder(
    llama_model* model_target,
    llama_context* ctx_target,
    const std::string& draft_model_path,
    const Config& config
) : model_tgt_(model_target),
    ctx_tgt_(ctx_target),
    config_(config),
    model_dft_(nullptr),
    ctx_dft_(nullptr)
{
    if (!model_tgt_ || !ctx_tgt_) {
        std::cerr << "[SpecDecoder] ERROR: Invalid target model/context\n";
        return;
    }

    std::cout << "[SpecDecoder] Initializing Speculative Decoder...\n";
    std::cout << "[SpecDecoder] Draft model path: " << draft_model_path << "\n";

    // 1. 加载 draft 模型
    llama_model_params model_params = llama_model_default_params();
    model_dft_ = llama_model_load_from_file(draft_model_path.c_str(), model_params);

    if (!model_dft_) {
        std::cerr << "[SpecDecoder] ERROR: Failed to load draft model: " << draft_model_path << "\n";
        return;
    }

    std::cout << "[SpecDecoder] Draft model loaded successfully\n";

    // 2. 创建 draft 上下文
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = config_.n_ctx_draft;
    ctx_params.n_threads = config_.n_threads_draft;
    ctx_params.n_batch = 512;  // 足够大以支持长prompt (最多512 tokens)

    ctx_dft_ = llama_init_from_model(model_dft_, ctx_params);

    if (!ctx_dft_) {
        std::cerr << "[SpecDecoder] ERROR: Failed to create draft context\n";
        llama_model_free(model_dft_);
        model_dft_ = nullptr;
        return;
    }

    std::cout << "[SpecDecoder] Draft context created (n_ctx=" << config_.n_ctx_draft
              << ", n_threads=" << config_.n_threads_draft << ")\n";

    // 3. 验证词汇表兼容性
    const llama_vocab* vocab_tgt = llama_model_get_vocab(model_tgt_);
    const llama_vocab* vocab_dft = llama_model_get_vocab(model_dft_);

    const int vocab_type_tgt = llama_vocab_type(vocab_tgt);
    const int vocab_type_dft = llama_vocab_type(vocab_dft);

    if (vocab_type_tgt != vocab_type_dft) {
        std::cerr << "[SpecDecoder] WARNING: Vocab types differ (target="
                  << vocab_type_tgt << ", draft=" << vocab_type_dft << ")\n";
    }

    const int n_vocab_tgt = llama_vocab_n_tokens(vocab_tgt);
    const int n_vocab_dft = llama_vocab_n_tokens(vocab_dft);
    const int vocab_diff = std::abs(n_vocab_tgt - n_vocab_dft);

    if (vocab_diff > 128) {  // 允许一定差异
        std::cerr << "[SpecDecoder] WARNING: Large vocab size difference: " << vocab_diff << "\n";
    }

    std::cout << "[SpecDecoder] Vocab check: target=" << n_vocab_tgt
              << " tokens, draft=" << n_vocab_dft << " tokens (diff=" << vocab_diff << ")\n";

    // 4. 初始化统计
    resetStats();

    // 5. 初始化任务分类器（如果启用）
    if (config_.enable_task_aware) {
        task_classifier_ = std::make_unique<TaskClassifier>(config_.task_aware_verbose);
        std::cout << "[SpecDecoder] Task-aware optimization enabled\n";
    }

    // 6. 初始化置信度引导（如果启用）
    if (config_.enable_confidence_guide) {
        ConfidenceGuidedStrategy::Config conf_config;
        conf_config.high_threshold = config_.confidence_high_threshold;
        conf_config.low_threshold = config_.confidence_low_threshold;
        conf_config.min_n_draft = config_.confidence_min_n_draft;
        conf_config.max_n_draft = config_.confidence_max_n_draft;
        conf_config.enable_verbose = config_.confidence_verbose;

        confidence_strategy_ = std::make_unique<ConfidenceGuidedStrategy>(conf_config);
        confidence_analyzer_ = std::make_unique<ConfidenceAnalyzer>();
        std::cout << "[SpecDecoder] Confidence-guided optimization enabled\n";
    }

    std::cout << "[SpecDecoder] ✅ Initialization complete!\n";
    std::cout << "[SpecDecoder] Config: n_draft=" << config_.n_draft
              << ", n_draft_min=" << config_.n_draft_min
              << ", p_min=" << config_.p_min << "\n";
}

SpeculativeDecoder::~SpeculativeDecoder() {
    if (ctx_dft_) {
        llama_free(ctx_dft_);
        ctx_dft_ = nullptr;
    }
    if (model_dft_) {
        llama_model_free(model_dft_);
        model_dft_ = nullptr;
    }
    std::cout << "[SpecDecoder] Cleanup complete\n";
}

// ============================================================
// 辅助函数：Tokenization
// ============================================================

std::vector<llama_token> SpeculativeDecoder::tokenize(const std::string& text) const {
    if (!model_tgt_) return {};

    const llama_vocab* vocab = llama_model_get_vocab(model_tgt_);

    // 预分配足够大的缓冲区
    std::vector<llama_token> tokens(text.size() * 4);

    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        text.size(),
        tokens.data(),
        tokens.size(),
        true,   // add_special (add BOS)
        false   // parse_special
    );

    if (n_tokens < 0) {
        // 缓冲区太小，重新分配
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            text.size(),
            tokens.data(),
            tokens.size(),
            true,
            false
        );
    }

    if (n_tokens < 0) {
        std::cerr << "[SpecDecoder] ERROR: Tokenization failed\n";
        return {};
    }

    tokens.resize(n_tokens);
    return tokens;
}

std::string SpeculativeDecoder::detokenize(llama_token token) const {
    if (!model_tgt_) return "";

    const llama_vocab* vocab = llama_model_get_vocab(model_tgt_);

    char buffer[256] = {0};
    int n = llama_token_to_piece(vocab, token, buffer, sizeof(buffer), 0, false);

    if (n < 0) {
        std::cerr << "[SpecDecoder] ERROR: Detokenization failed for token " << token << "\n";
        return "";
    }

    return std::string(buffer, n);
}

std::string SpeculativeDecoder::detokenize(const std::vector<llama_token>& tokens) const {
    std::string result;
    result.reserve(tokens.size() * 4);

    for (llama_token token : tokens) {
        result += detokenize(token);
    }

    return result;
}

// ============================================================
// 辅助函数：采样
// ============================================================

llama_token SpeculativeDecoder::sampleGreedy(llama_context* ctx) {
    if (!ctx) return -1;

    const llama_vocab* vocab = llama_model_get_vocab(
        ctx == ctx_tgt_ ? model_tgt_ : model_dft_
    );
    const int n_vocab = llama_vocab_n_tokens(vocab);
    const float* logits = llama_get_logits(ctx);

    if (!logits) {
        std::cerr << "[SpecDecoder] ERROR: No logits available\n";
        return -1;
    }

    // 贪婪采样：选择概率最大的 token
    int best_token = 0;
    float best_logit = logits[0];

    for (int i = 1; i < n_vocab; ++i) {
        if (logits[i] > best_logit) {
            best_logit = logits[i];
            best_token = i;
        }
    }

    return best_token;
}

llama_token SpeculativeDecoder::sampleToken(
    llama_context* ctx,
    float temperature,
    int top_k,
    float top_p
) {
    if (!ctx) return -1;

    // 简化实现：使用贪婪采样
    // TODO: 实现完整的 top-k/top-p 采样
    return sampleGreedy(ctx);
}

float SpeculativeDecoder::getTokenProb(llama_context* ctx, llama_token token) {
    if (!ctx) return 0.0f;

    const llama_vocab* vocab = llama_model_get_vocab(
        ctx == ctx_tgt_ ? model_tgt_ : model_dft_
    );
    const int n_vocab = llama_vocab_n_tokens(vocab);

    if (token < 0 || token >= n_vocab) return 0.0f;

    const float* logits = llama_get_logits(ctx);
    if (!logits) return 0.0f;

    // 计算 softmax 概率
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += std::exp(logits[i] - max_logit);
    }

    float prob = std::exp(logits[token] - max_logit) / sum_exp;
    return prob;
}

// 从指定的logits数组计算token概率 (修复版本)
float SpeculativeDecoder::getTokenProbFromLogits(
    const float* logits,
    int n_vocab,
    llama_token token
) {
    if (!logits || token < 0 || token >= n_vocab) return 0.0f;

    // 计算 softmax 概率
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += std::exp(logits[i] - max_logit);
    }

    float prob = std::exp(logits[token] - max_logit) / sum_exp;
    return prob;
}

// 从指定的logits数组采样token (完整实现)
llama_token SpeculativeDecoder::sampleTokenFromLogits(
    const float* logits,
    int n_vocab,
    float temperature
) {
    if (!logits) return -1;

    if (temperature < 0.01f) {
        // 贪婪采样
        int best_token = 0;
        float best_logit = logits[0];
        for (int i = 1; i < n_vocab; ++i) {
            if (logits[i] > best_logit) {
                best_logit = logits[i];
                best_token = i;
            }
        }
        return best_token;
    } else {
        // 完整的temperature采样实现

        // 1. 找到最大logit (数值稳定性)
        float max_logit = logits[0];
        for (int i = 1; i < n_vocab; ++i) {
            max_logit = std::max(max_logit, logits[i]);
        }

        // 2. 应用temperature并计算exp (softmax)
        std::vector<float> probs(n_vocab);
        float sum_exp = 0.0f;
        for (int i = 0; i < n_vocab; ++i) {
            probs[i] = std::exp((logits[i] - max_logit) / temperature);
            sum_exp += probs[i];
        }

        // 3. 归一化概率
        for (int i = 0; i < n_vocab; ++i) {
            probs[i] /= sum_exp;
        }

        // 4. 随机采样 (基于累积分布)
        float rand_val = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        float cumsum = 0.0f;
        for (int i = 0; i < n_vocab; ++i) {
            cumsum += probs[i];
            if (rand_val < cumsum) {
                return static_cast<llama_token>(i);
            }
        }

        // Fallback (数值误差)
        return static_cast<llama_token>(n_vocab - 1);
    }
}

// ============================================================
// 核心算法：Draft 生成
// ============================================================

std::vector<llama_token> SpeculativeDecoder::genDraft(
    const std::vector<llama_token>& prompt,
    llama_token last_token,
    int n_past
) {
    if (!ctx_dft_) {
        if (config_.verbose) {
            std::cerr << "[Draft] ERROR: Draft context not initialized\n";
        }
        return {};
    }

    std::vector<llama_token> draft;
    draft.reserve(config_.n_draft);

    const llama_vocab* vocab_dft = llama_model_get_vocab(model_dft_);
    const int n_vocab = llama_vocab_n_tokens(vocab_dft);

    if (config_.verbose) {
        std::cerr << "[Draft] Generating up to " << config_.n_draft << " tokens...\n";
    }

    // 用于收集本轮draft的置信度数据
    std::vector<float> draft_confidences;
    draft_confidences.reserve(config_.n_draft);

    // 自回归生成 N 个 draft tokens
    llama_token current = last_token;

    for (int i = 0; i < config_.n_draft; ++i) {
        // 构造 batch（单个 token）
        llama_batch batch = llama_batch_init(1, 0, 1);
        batch.token[0] = current;
        batch.pos[0] = n_past + i;
        batch.seq_id[0][0] = 0;
        batch.n_seq_id[0] = 1;
        batch.logits[0] = 1;
        batch.n_tokens = 1;

        // 推理
        if (llama_decode(ctx_dft_, batch) != 0) {
            if (config_.verbose) {
                std::cerr << "[Draft] Decode failed at step " << i << "\n";
            }
            llama_batch_free(batch);
            break;
        }
        llama_batch_free(batch);

        // 采样下一个 token
        // 边缘计算优化: 使用greedy采样确保最高接受率
        llama_token next = sampleToken(ctx_dft_, 0.0f, config_.top_k, config_.top_p);  // temperature=0 (greedy)

        if (next < 0) {
            if (config_.verbose) {
                std::cerr << "[Draft] Sampling failed at step " << i << "\n";
            }
            break;
        }

        // ============ Phase 2: Token置信度计算 ============
        float token_confidence = 0.0f;
        if (config_.enable_confidence_guide) {
            const float* logits = llama_get_logits(ctx_dft_);
            if (logits && n_vocab > 0) {
                std::vector<float> logits_vec(logits, logits + n_vocab);
                token_confidence = TokenConfidenceCalculator::calculate(logits_vec);
                draft_confidences.push_back(token_confidence);

                if (config_.confidence_verbose) {
                    std::cerr << "[ConfidenceGuide] Token " << i
                              << " confidence: " << token_confidence << "\n";
                }
            }
        }

        // 置信度检查（可选）
        if (config_.p_min > 0.0f) {
            float prob = getTokenProb(ctx_dft_, next);
            if (prob < config_.p_min) {
                if (config_.verbose) {
                    std::cerr << "[Draft] Low confidence " << prob << " < " << config_.p_min
                              << " at step " << i << ", stopping\n";
                }
                break;
            }
        }

        draft.push_back(next);
        current = next;

        // EOS 检查
        if (llama_vocab_is_eog(vocab_dft, next)) {
            if (config_.verbose) {
                std::cerr << "[Draft] EOS detected at step " << i << "\n";
            }
            break;
        }
    }

    // ============ Phase 2: 基于置信度调整下一轮的n_draft ============
    if (config_.enable_confidence_guide && !draft_confidences.empty() && confidence_strategy_) {
        float avg_confidence = std::accumulate(draft_confidences.begin(),
                                              draft_confidences.end(), 0.0f)
                              / draft_confidences.size();

        int new_n_draft = confidence_strategy_->adjustDraftSize(avg_confidence, config_.n_draft);

        if (new_n_draft != config_.n_draft) {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.n_confidence_adjustments++;
            config_.n_draft = new_n_draft;
        }

        // 更新统计数据
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.n_confidence_samples += draft_confidences.size();

        float sum = std::accumulate(draft_confidences.begin(), draft_confidences.end(), 0.0f);
        stats_.average_confidence = (stats_.average_confidence * (stats_.n_confidence_samples - draft_confidences.size())
                                    + sum) / stats_.n_confidence_samples;

        stats_.min_confidence = std::min(stats_.min_confidence,
                                        *std::min_element(draft_confidences.begin(), draft_confidences.end()));
        stats_.max_confidence = std::max(stats_.max_confidence,
                                        *std::max_element(draft_confidences.begin(), draft_confidences.end()));
    }

    if (config_.verbose) {
        std::cerr << "[Draft] Generated " << draft.size() << " tokens\n";
    }

    return draft;
}

// ============================================================
// 核心算法：验证与接受
// ============================================================

std::vector<llama_token> SpeculativeDecoder::verifyAndAccept(
    const std::vector<llama_token>& draft,
    llama_token last_token,
    int n_past,
    float temperature
) {
    if (!ctx_tgt_) {
        std::cerr << "[Verify] ERROR: Target context not initialized\n";
        return {};
    }

    std::vector<llama_token> accepted;
    float temp = temperature;  // Shorthand for readability

    // 跳过太短的 draft
    if ((int)draft.size() < config_.n_draft_min) {
        if (config_.verbose) {
            std::cerr << "[Verify] Draft too short (" << draft.size()
                      << " < " << config_.n_draft_min << "), skipping\n";
        }
        return accepted;
    }

    // 1. 构造 batch: [last_token, draft[0], draft[1], ..., draft[N-1]]
    int batch_size = 1 + draft.size();
    llama_batch batch = llama_batch_init(batch_size, 0, 1);

    // 添加 last_token
    batch.token[0] = last_token;
    batch.pos[0] = n_past;
    batch.seq_id[0][0] = 0;
    batch.n_seq_id[0] = 1;
    batch.logits[0] = 1;

    // 添加 draft tokens
    for (size_t i = 0; i < draft.size(); ++i) {
        batch.token[i + 1] = draft[i];
        batch.pos[i + 1] = n_past + 1 + i;
        batch.seq_id[i + 1][0] = 0;
        batch.n_seq_id[i + 1] = 1;
        batch.logits[i + 1] = 1;
    }
    batch.n_tokens = batch_size;

    if (config_.verbose) {
        std::cerr << "[Verify] Batch size: " << batch_size << " (1 + " << draft.size() << " draft)\n";
    }

    // 2. 大模型验证
    if (llama_decode(ctx_tgt_, batch) != 0) {
        std::cerr << "[Verify] ERROR: Decode failed\n";
        llama_batch_free(batch);
        return accepted;
    }

    // 3. 逐个验证并接受 (优化版本: 基于概率阈值)
    const llama_vocab* vocab_tgt = llama_model_get_vocab(model_tgt_);
    const int n_vocab = llama_vocab_n_tokens(vocab_tgt);
    const float PROB_THRESHOLD = 0.001f;  // 概率阈值: 0.1%以上即接受 (降低以适配Q2模型)

    // CRITICAL FIX: Position i的logits对应的是draft[i]的验证，而不是draft[i-1]!
    // batch[0]=last_token → logits[0]用于验证draft[0]
    // batch[1]=draft[0]   → logits[1]用于验证draft[1]
    // batch[2]=draft[1]   → logits[2]用于验证draft[2]

    for (size_t i = 0; i < batch_size; ++i) {
        // 获取当前位置的 logits
        const float* logits = llama_get_logits_ith(ctx_tgt_, i);
        if (!logits) {
            if (config_.verbose) {
                std::cerr << "[Verify] No logits at position " << i << "\n";
            }
            break;
        }

        // 检查是否还有draft token需要验证
        if (i >= draft.size()) {
            // 所有draft都验证完了，这是最后一个位置，采样新token
            llama_token sampled = sampleTokenFromLogits(logits, n_vocab, temp);
            if (sampled < 0) {
                if (config_.verbose) {
                    std::cerr << "[Verify] Sampling failed at position " << i << "\n";
                }
                break;
            }
            accepted.push_back(sampled);
            if (config_.verbose) {
                std::cerr << "[Verify] Position " << i << ": sampled new token " << sampled << " (temp=" << temp << ")\n";
            }
            break;  // 只采样一个新token
        }

        // 验证 draft token (修复: position i 验证 draft[i])
        llama_token draft_token = draft[i];

        // 计算draft token在target分布中的概率
        float draft_prob = getTokenProbFromLogits(logits, n_vocab, draft_token);

        if (draft_prob >= PROB_THRESHOLD) {
            // 接受: draft token概率足够高
            accepted.push_back(draft_token);
            if (config_.verbose) {
                std::cerr << "[Verify] ✓ Position " << i << ": accepted draft[" << i
                          << "] = " << draft_token << " (prob=" << (draft_prob * 100.0f) << "%)\n";
            }
        } else {
            // 拒绝: 概率太低，采样新token (使用相同温度)
            llama_token sampled = sampleTokenFromLogits(logits, n_vocab, temp);  // 使用temp!
            if (sampled < 0) {
                if (config_.verbose) {
                    std::cerr << "[Verify] Sampling failed at position " << i << "\n";
                }
                break;
            }
            accepted.push_back(sampled);
            if (config_.verbose) {
                std::cerr << "[Verify] ✗ Position " << i << ": rejected draft[" << i
                          << "] = " << draft_token << " (prob=" << (draft_prob * 100.0f)
                          << "% < " << (PROB_THRESHOLD * 100.0f) << "%), sampled " << sampled << " (temp=" << temp << ") instead\n";
            }
            break;  // 拒绝后终止
        }
    }

    llama_batch_free(batch);

    if (config_.verbose) {
        std::cerr << "[Verify] Accepted " << accepted.size() << " / " << (draft.size() + 1)
                  << " tokens (" << (accepted.size() - 1) << " draft)\n";
    }

    return accepted;
}

// ============================================================
// 主推理循环
// ============================================================

std::vector<llama_token> SpeculativeDecoder::inferTokens(
    const std::vector<llama_token>& prompt_tokens,
    int max_tokens,
    float temperature
) {
    if (!ctx_tgt_ || !ctx_dft_) {
        std::cerr << "[SpecDecoder] ERROR: Models not initialized\n";
        return {};
    }

    std::vector<llama_token> result;
    result.reserve(max_tokens);

    // 使用配置中的 temperature，除非显式指定
    // 注意: temperature=0.0是有效值(greedy)，只有负值才使用默认值
    float temp = (temperature < 0.0f) ? config_.temperature : temperature;

    // ============ 任务感知优化 ============
    // 根据prompt内容自动调整配置
    if (config_.enable_task_aware && task_classifier_) {
        // 将tokens转换为文本用于分类
        std::string prompt_text = detokenize(prompt_tokens);

        // 分类任务类型
        auto classification = task_classifier_->classify(prompt_text);

        // 应用任务特定配置
        config_.n_draft = classification.config.n_draft;
        config_.n_draft_min_adaptive = classification.config.n_draft_min;
        config_.n_draft_max = classification.config.n_draft_max;
        config_.accept_rate_high = classification.config.accept_rate_high;
        config_.accept_rate_low = classification.config.accept_rate_low;

        // 更新统计信息
        {
            std::lock_guard<std::mutex> g(stats_mutex_);
            stats_.detected_task_type = taskTypeToString(classification.task_type);
            stats_.task_classification_confidence = classification.confidence;
            stats_.n_draft_current = config_.n_draft;
        }

        if (config_.verbose || config_.task_aware_verbose) {
            std::cout << "[SpecDecoder] Task detected: " << taskTypeToString(classification.task_type)
                      << " (confidence: " << (classification.confidence * 100.0f) << "%)\n";
            std::cout << "[SpecDecoder] Applied task-specific config: n_draft=" << config_.n_draft
                      << ", accept_rate_range=[" << config_.accept_rate_low
                      << ", " << config_.accept_rate_high << "]\n";
        }
    }

    std::cout << "[SpecDecoder] Starting inference: max_tokens=" << max_tokens
              << ", temperature=" << temp << "\n";

    // 设置随机数种子 (确保可重复性，但draft和target仍会有随机性)
    // 注意: 推测式解码依赖概率验证，不需要完全相同的随机序列
    srand(static_cast<unsigned int>(time(nullptr)));

    // Clear KV cache before each inference to avoid position conflicts
    llama_memory_clear(llama_get_memory(ctx_tgt_), true);
    llama_memory_clear(llama_get_memory(ctx_dft_), true);

    auto t_start = now();

    // 1. 处理 prompt（除最后一个 token）
    if (prompt_tokens.size() < 2) {
        std::cerr << "[SpecDecoder] ERROR: Prompt too short\n";
        return {};
    }

    int prompt_size = prompt_tokens.size() - 1;
    llama_batch batch_prompt = llama_batch_init(prompt_size, 0, 1);

    for (int i = 0; i < prompt_size; ++i) {
        batch_prompt.token[i] = prompt_tokens[i];
        batch_prompt.pos[i] = i;
        batch_prompt.seq_id[i][0] = 0;
        batch_prompt.n_seq_id[i] = 1;
        batch_prompt.logits[i] = (i == prompt_size - 1);
    }
    batch_prompt.n_tokens = prompt_size;

    std::cout << "[SpecDecoder] Processing prompt (" << prompt_size << " tokens)...\n";

    // Feed prompt to TARGET model
    if (llama_decode(ctx_tgt_, batch_prompt) != 0) {
        std::cerr << "[SpecDecoder] ERROR: Prompt decode failed (target)\n";
        llama_batch_free(batch_prompt);
        return {};
    }

    // Feed prompt to DRAFT model as well (CRITICAL FIX)
    if (llama_decode(ctx_dft_, batch_prompt) != 0) {
        std::cerr << "[SpecDecoder] ERROR: Prompt decode failed (draft)\n";
        llama_batch_free(batch_prompt);
        return {};
    }

    llama_batch_free(batch_prompt);

    llama_token last_token = prompt_tokens.back();
    int n_past = prompt_size;
    int n_predict = 0;

    const llama_vocab* vocab_tgt = llama_model_get_vocab(model_tgt_);

    std::cout << "[SpecDecoder] Starting generation loop...\n";

    // 2. 主循环
    while (n_predict < max_tokens) {
        auto t_iter_start = now();

        // (1) Draft 生成
        auto t_draft_start = now();
        std::vector<llama_token> draft = genDraft(prompt_tokens, last_token, n_past);
        auto t_draft_end = now();

        stats_.n_drafted += draft.size();
        stats_.time_draft_ms += elapsedMs(t_draft_start, t_draft_end);

        // (2) Verify 并接受 (边缘计算优化: 使用greedy确保最高接受率)
        auto t_verify_start = now();
        std::vector<llama_token> accepted = verifyAndAccept(draft, last_token, n_past, 0.0f);  // greedy
        auto t_verify_end = now();

        stats_.time_verify_ms += elapsedMs(t_verify_start, t_verify_end);

        // Fallback: 如果 draft 太短或全部被拒绝，单步推理
        if (accepted.empty()) {
            if (config_.verbose) {
                std::cerr << "[SpecDecoder] No accepted tokens, falling back to single-step\n";
            }

            llama_batch batch_single = llama_batch_init(1, 0, 1);
            batch_single.token[0] = last_token;
            batch_single.pos[0] = n_past;
            batch_single.seq_id[0][0] = 0;
            batch_single.n_seq_id[0] = 1;
            batch_single.logits[0] = 1;
            batch_single.n_tokens = 1;

            if (llama_decode(ctx_tgt_, batch_single) != 0) {
                std::cerr << "[SpecDecoder] ERROR: Fallback decode failed\n";
                llama_batch_free(batch_single);
                break;
            }

            llama_token next = sampleToken(ctx_tgt_, temp, config_.top_k, config_.top_p);
            llama_batch_free(batch_single);

            if (next < 0) {
                std::cerr << "[SpecDecoder] ERROR: Fallback sampling failed\n";
                break;
            }

            accepted.push_back(next);
        }

        // (3) 更新统计
        // accepted 包含：第一个总是新采样的，后续是被接受的 draft
        stats_.n_accepted += (accepted.size() - 1);
        stats_.n_predict += accepted.size();

        // (3.5) 自适应调整
        if (config_.enable_adaptive && draft.size() > 0) {
            // 计算当前轮次的接受率
            double current_accept_rate = (double)(accepted.size() - 1) / draft.size();

            // 更新滑动窗口
            updateAcceptRateWindow(current_accept_rate);

            // 每隔一定次数调整一次 draft 数量（避免过于频繁调整）
            if (n_predict % 5 == 0) {  // 每生成5个token检查一次
                adjustDraftCount();
            }
        }

        // (4) 输出并更新状态
        for (size_t i = 0; i < accepted.size(); ++i) {
            result.push_back(accepted[i]);
            last_token = accepted[i];

            // EOS 检测
            if (llama_vocab_is_eog(vocab_tgt, last_token)) {
                std::cout << "[SpecDecoder] EOS detected, stopping\n";
                goto done;
            }
        }

        n_past += accepted.size();
        n_predict += accepted.size();

        // (5) 清理未接受的 KV cache (both target and draft)
        llama_memory_seq_rm(llama_get_memory(ctx_tgt_), 0, n_past, -1);
        llama_memory_seq_rm(llama_get_memory(ctx_dft_), 0, n_past, -1);

        // 进度显示
        if (n_predict % 10 == 0 || config_.verbose) {
            std::cout << "[SpecDecoder] Generated " << n_predict << " / " << max_tokens
                      << " tokens (accept_rate=" << (stats_.n_drafted > 0 ?
                         (double)stats_.n_accepted / stats_.n_drafted : 0.0) << ")\n";
        }
    }

done:
    auto t_end = now();
    stats_.time_total_ms = elapsedMs(t_start, t_end);

    // 计算最终统计
    stats_.accept_rate = stats_.n_drafted > 0 ? (double)stats_.n_accepted / stats_.n_drafted : 0.0;

    // 估算加速比（相对于传统自回归）
    // Speedup = Time_normal / Time_spec
    // Time_normal ≈ n_predict × T_target
    // Time_spec = measured
    // 我们使用 verify 时间作为 T_target 的估计
    double avg_verify_time = stats_.n_predict > 0 ? stats_.time_verify_ms / stats_.n_predict : 1.0;
    double estimated_normal_time = stats_.n_predict * avg_verify_time;
    stats_.speedup = estimated_normal_time > 0 ? estimated_normal_time / stats_.time_total_ms : 1.0;

    std::cout << "[SpecDecoder] ✅ Generation complete!\n";
    printStats();

    return result;
}

std::string SpeculativeDecoder::infer(
    const std::string& prompt,
    int max_tokens,
    float temperature
) {
    std::cout << "[SpecDecoder] Tokenizing prompt...\n";
    auto tokens = tokenize(prompt);

    if (tokens.empty()) {
        std::cerr << "[SpecDecoder] ERROR: Tokenization failed\n";
        return "";
    }

    std::cout << "[SpecDecoder] Prompt: " << tokens.size() << " tokens\n";

    auto result_tokens = inferTokens(tokens, max_tokens, temperature);

    return detokenize(result_tokens);
}

// ============================================================
// 统计功能
// ============================================================

SpeculativeDecoder::Stats SpeculativeDecoder::getStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void SpeculativeDecoder::resetStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = Stats();
}

void SpeculativeDecoder::printStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    std::cout << "\n";
    std::cout << "========== Speculative Decoding Statistics ==========\n";
    std::cout << "Generated tokens:    " << stats_.n_predict << "\n";
    std::cout << "Drafted tokens:      " << stats_.n_drafted << "\n";
    std::cout << "Accepted tokens:     " << stats_.n_accepted << "\n";
    std::cout << "Accept rate:         " << (stats_.accept_rate * 100.0) << "%\n";
    std::cout << "Speedup:             " << stats_.speedup << "x\n";
    std::cout << "Time (draft):        " << stats_.time_draft_ms << " ms\n";
    std::cout << "Time (verify):       " << stats_.time_verify_ms << " ms\n";
    std::cout << "Time (total):        " << stats_.time_total_ms << " ms\n";
    std::cout << "Tokens per second:   " << stats_.tokens_per_sec() << "\n";
    std::cout << "Ms per token:        " << stats_.ms_per_token() << "\n";

    // 自适应统计（如果启用）
    if (config_.enable_adaptive) {
        std::cout << "\n--- Adaptive Statistics ---\n";
        std::cout << "Current n_draft:     " << stats_.n_draft_current << "\n";
        std::cout << "Recent accept rate:  " << (stats_.recent_accept_rate * 100.0) << "%\n";
        std::cout << "Total adjustments:   " << stats_.n_adjustments << "\n";
        std::cout << "  - Increased:       " << stats_.n_increased << "\n";
        std::cout << "  - Decreased:       " << stats_.n_decreased << "\n";
    }

    if (config_.enable_temperature_aware) {
        std::cout << "\n--- Temperature Aware ---\n";
        std::cout << "Fallback count:      " << stats_.n_temperature_fallback << "\n";
    }

    if (config_.enable_task_aware) {
        std::cout << "\n--- Task-Aware Optimization ---\n";
        std::cout << "Detected task:       " << stats_.detected_task_type << "\n";
        std::cout << "Confidence:          " << (stats_.task_classification_confidence * 100.0f) << "%\n";
    }

    if (config_.enable_confidence_guide && stats_.n_confidence_samples > 0) {
        std::cout << "\n--- Confidence-Guided Optimization ---\n";
        std::cout << "Average confidence:  " << std::fixed << std::setprecision(3)
                  << stats_.average_confidence << "\n";
        std::cout << "Min confidence:      " << stats_.min_confidence << "\n";
        std::cout << "Max confidence:      " << stats_.max_confidence << "\n";
        std::cout << "Confidence samples:  " << stats_.n_confidence_samples << "\n";
        std::cout << "Adjustments:         " << stats_.n_confidence_adjustments << "\n";
    }

    std::cout << "====================================================\n";
    std::cout << "\n";
}

// ============================================================
// 自适应推测方法实现
// ============================================================

void SpeculativeDecoder::updateAcceptRateWindow(double current_accept_rate) const {
    std::lock_guard<std::mutex> g(adaptive_mutex_);

    // 添加当前接受率到滑动窗口
    accept_rate_window_.push_back(current_accept_rate);

    // 保持窗口大小在配置的范围内
    while ((int)accept_rate_window_.size() > config_.window_size) {
        accept_rate_window_.pop_front();
    }

    if (config_.verbose) {
        std::cerr << "[Adaptive] Window size: " << accept_rate_window_.size()
                  << ", Current accept rate: " << (current_accept_rate * 100.0) << "%\n";
    }
}

double SpeculativeDecoder::calculateRecentAcceptRate() const {
    std::lock_guard<std::mutex> g(adaptive_mutex_);

    if (accept_rate_window_.empty()) {
        return 0.0;
    }

    // 计算滑动窗口内的平均接受率
    double sum = std::accumulate(accept_rate_window_.begin(),
                                  accept_rate_window_.end(),
                                  0.0);
    return sum / accept_rate_window_.size();
}

void SpeculativeDecoder::adjustDraftCount() {
    if (!config_.enable_adaptive) {
        return;  // 未启用自适应，直接返回
    }

    std::lock_guard<std::mutex> g(adaptive_mutex_);

    // 窗口数据不足，暂不调整
    if ((int)accept_rate_window_.size() < config_.window_size / 2) {
        return;
    }

    // 计算最近的平均接受率
    double recent_rate = 0.0;
    for (double rate : accept_rate_window_) {
        recent_rate += rate;
    }
    recent_rate /= accept_rate_window_.size();

    // 更新统计信息
    {
        std::lock_guard<std::mutex> stats_g(stats_mutex_);
        stats_.recent_accept_rate = recent_rate;
    }

    int old_n_draft = config_.n_draft;

    // 根据接受率调整 draft 数量
    if (recent_rate > config_.accept_rate_high) {
        // 接受率高 → 增加 draft 数量
        config_.n_draft = std::min(config_.n_draft_max,
                                   config_.n_draft + config_.adjust_step);

        if (config_.n_draft != old_n_draft) {
            std::lock_guard<std::mutex> stats_g(stats_mutex_);
            stats_.n_adjustments++;
            stats_.n_increased++;
        }
    }
    else if (recent_rate < config_.accept_rate_low) {
        // 接受率低 → 减少 draft 数量
        config_.n_draft = std::max(config_.n_draft_min_adaptive,
                                   config_.n_draft - config_.adjust_step);

        if (config_.n_draft != old_n_draft) {
            std::lock_guard<std::mutex> stats_g(stats_mutex_);
            stats_.n_adjustments++;
            stats_.n_decreased++;
        }
    }

    // 输出调整日志
    if (config_.verbose && config_.n_draft != old_n_draft) {
        std::cerr << "[Adaptive] Adjusted n_draft: " << old_n_draft
                  << " → " << config_.n_draft
                  << " (recent accept rate: " << (recent_rate * 100.0) << "%)\n";
    }

    // 更新当前 draft 数量到统计
    {
        std::lock_guard<std::mutex> stats_g(stats_mutex_);
        stats_.n_draft_current = config_.n_draft;
    }
}

bool SpeculativeDecoder::shouldFallbackDueToTemperature(float temperature) const {
    if (!config_.enable_temperature_aware) {
        return false;  // 未启用温度感知，不回退
    }

    // 检查温度是否超过阈值
    if (temperature > config_.temperature_threshold) {
        if (config_.verbose) {
            std::cerr << "[Adaptive] Temperature " << temperature
                      << " exceeds threshold " << config_.temperature_threshold
                      << ", fallback to normal inference recommended\n";
        }
        return true;
    }

    return false;
}

void SpeculativeDecoder::recordTemperatureFallback() const {
    std::lock_guard<std::mutex> g(stats_mutex_);
    stats_.n_temperature_fallback++;
}
