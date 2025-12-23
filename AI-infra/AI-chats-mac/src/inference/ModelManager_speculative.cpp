// ============================================================
// ModelManager 推测式解码扩展实现
// 这个文件包含推测式解码相关的函数实现
// ============================================================

#include "ModelManager.h"
#include "SpeculativeDecoder.h"
#include <iostream>

// ============================================================
// 加载 Draft 模型
// ============================================================

bool ModelManager::loadDraftModel(const std::string& draft_model_path) {
    std::lock_guard<std::mutex> g(spec_mutex_);

    if (!model_ || !ctx_) {
        std::cerr << "[ModelManager] ERROR: Target model not loaded. Call loadModel() first.\n";
        return false;
    }

    std::cout << "[ModelManager] Loading draft model for speculative decoding...\n";
    std::cout << "[ModelManager] Draft model path: " << draft_model_path << "\n";

    try {
        // 配置推测式解码器
        SpeculativeDecoder::Config config;
        config.n_draft = 16;              // Draft tokens 数量
        config.n_draft_min = 5;           // 最小 draft 数量
        config.p_min = 0.9f;              // 置信度阈值
        config.n_ctx_draft = n_ctx_;      // 使用与 target 相同的上下文长度
        config.n_threads_draft = std::max(1, n_threads_ / 2);  // Draft 用一半线程
        config.temperature = 0.7f;        // 默认温度
        config.verbose = false;           // 关闭详细日志

        // 启用高级优化
        config.enable_adaptive = true;           // 自适应draft数量
        config.enable_temperature_aware = true;  // 温度感知fallback
        config.enable_task_aware = true;         // 任务感知优化（新增）
        config.task_aware_verbose = false;       // 任务分类详细日志

        // 创建推测式解码器
        spec_decoder_ = std::make_unique<SpeculativeDecoder>(
            model_,
            ctx_,
            draft_model_path,
            config
        );

        if (!spec_decoder_->isDraftModelLoaded()) {
            std::cerr << "[ModelManager] ERROR: Failed to initialize speculative decoder\n";
            spec_decoder_.reset();
            return false;
        }

        std::cout << "[ModelManager] ✅ Draft model loaded successfully\n";
        std::cout << "[ModelManager] Speculative decoding is now available\n";
        std::cout << "[ModelManager] Use setSpeculativeMode(true) to enable it\n";

        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[ModelManager] ERROR: Exception while loading draft model: " << e.what() << "\n";
        spec_decoder_.reset();
        return false;
    }
}

// ============================================================
// 启用/禁用推测式解码
// ============================================================

void ModelManager::setSpeculativeMode(bool enable) {
    std::lock_guard<std::mutex> g(spec_mutex_);

    if (enable && !spec_decoder_) {
        std::cerr << "[ModelManager] WARNING: Cannot enable speculative mode - draft model not loaded\n";
        std::cerr << "[ModelManager] Please call loadDraftModel() first\n";
        return;
    }

    enable_speculative_ = enable;

    if (enable) {
        std::cout << "[ModelManager] ✅ Speculative decoding ENABLED\n";
    } else {
        std::cout << "[ModelManager] ⏸️  Speculative decoding DISABLED\n";
    }
}

// ============================================================
// 检查推测式解码是否可用
// ============================================================

bool ModelManager::isSpeculativeEnabled() const {
    std::lock_guard<std::mutex> g(spec_mutex_);
    return enable_speculative_ && spec_decoder_ != nullptr;
}

// ============================================================
// 推测式解码推理
// ============================================================

std::string ModelManager::inferSpeculative(
    const std::string& chat_id,
    const std::string& user_msg,
    int maxTokens,
    float temperature
) {
    // 1. 检查推测式解码是否可用
    {
        std::lock_guard<std::mutex> g(spec_mutex_);
        if (!enable_speculative_ || !spec_decoder_) {
            std::cerr << "[ModelManager] WARNING: Speculative mode not enabled, falling back to normal inference\n";
            return infer(chat_id, user_msg, maxTokens, temperature);
        }

        // 2. 温度感知检查：如果温度过高，回退到传统推理
        if (spec_decoder_->shouldFallbackDueToTemperature(temperature)) {
            std::cerr << "[ModelManager] WARNING: Temperature too high (" << temperature
                      << "), falling back to normal inference\n";

            // 记录fallback事件
            spec_decoder_->recordTemperatureFallback();

            return infer(chat_id, user_msg, maxTokens, temperature);
        }
    }

    // 2. 保存用户消息到会话历史
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        chat_sessions_[chat_id].add("user", user_msg);
    }

    // 3. 构造 prompt
    std::string prompt;
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        prompt = chat_sessions_[chat_id].makePrompt();
    }

    std::cout << "[ModelManager] Using speculative decoding for inference...\n";
    std::cout << "[ModelManager] Prompt length: " << prompt.size() << " chars\n";

    // 4. 使用推测式解码推理
    std::string answer;
    {
        std::lock_guard<std::mutex> g(spec_mutex_);
        answer = spec_decoder_->infer(prompt, maxTokens, temperature);
    }

    // 5. 保存 assistant 回复到历史
    {
        std::lock_guard<std::mutex> g(chat_mutex_);
        chat_sessions_[chat_id].add("assistant", answer);
    }

    // 6. 打印统计信息
    {
        std::lock_guard<std::mutex> g(spec_mutex_);
        auto stats = spec_decoder_->getStats();
        std::cout << "[ModelManager] Speculative stats: "
                  << "tokens=" << stats.n_predict
                  << ", accept_rate=" << (stats.accept_rate * 100.0) << "%"
                  << ", speedup=" << stats.speedup << "x\n";
    }

    return answer;
}

// ============================================================
// 获取推测式解码统计信息
// ============================================================

ModelManager::SpeculativeStats ModelManager::getSpeculativeStats() const {
    std::lock_guard<std::mutex> g(spec_mutex_);

    SpeculativeStats result;

    if (spec_decoder_) {
        auto stats = spec_decoder_->getStats();
        result.n_predict = stats.n_predict;
        result.n_drafted = stats.n_drafted;
        result.n_accepted = stats.n_accepted;
        result.accept_rate = stats.accept_rate;
        result.speedup = stats.speedup;
        result.time_total_ms = stats.time_total_ms;
    }

    return result;
}
