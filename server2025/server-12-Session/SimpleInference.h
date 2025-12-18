/*==========================================================
 * SimpleInference - 简单推理类
 *
 * 功能：
 * 1. 加载GGUF格式的量化模型
 * 2. 单轮文本生成（无历史记录）
 * 3. 使用贪婪采样策略
 *
 * 依赖：llama.cpp库
 *=========================================================*/

#pragma once
#include "llama.h"
#include <string>
#include <vector>
#include <iostream>

class SimpleInference {
public:
    SimpleInference() : model_(nullptr) {}

    ~SimpleInference() {
        if (model_) {
            llama_free_model(model_);
        }
    }

    /**
     * @brief 加载GGUF格式的量化模型
     * @param model_path 模型文件路径（如 "../models/tinyllama-q4.gguf"）
     * @return 成功返回true，失败返回false
     */
    bool loadModel(const std::string& model_path) {
        // 释放旧模型
        if (model_) {
            llama_free_model(model_);
            model_ = nullptr;
        }

        // ★ 检测测试模式（路径包含"mock"）
        if (model_path.find("mock") != std::string::npos) {
            std::cout << "[SimpleInference] TEST MODE: Mock model detected\n";
            std::cout << "[SimpleInference] Session management APIs will work, inference will return mock responses\n";
            return true;  // 测试模式：跳过真实模型加载
        }

        // 设置模型参数
        llama_model_params model_params = llama_model_default_params();

        // 加载模型
        std::cout << "[SimpleInference] Loading model: " << model_path << "\n";
        model_ = llama_load_model_from_file(model_path.c_str(), model_params);

        if (!model_) {
            std::cerr << "[SimpleInference] Failed to load model: " << model_path << "\n";
            return false;
        }

        std::cout << "[SimpleInference] Model loaded successfully!\n";
        return true;
    }

    /**
     * @brief 单轮文本生成（无历史记录）
     * @param prompt 输入文本
     * @param max_tokens 最大生成token数（默认64）
     * @return 生成的文本
     */
    std::string generate(const std::string& prompt, int max_tokens = 64) {
        if (!model_) {
            // ★ 测试模式：返回mock响应
            return "[TEST MODE] Mock response to: " + prompt.substr(0, 50) + "...";
        }

        // 1. 创建context
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;       // 上下文窗口大小
        ctx_params.n_threads = 4;      // CPU线程数
        ctx_params.n_batch = 512;      // batch大小

        llama_context* ctx = llama_new_context_with_model(model_, ctx_params);
        if (!ctx) {
            return "[ERROR] Failed to create context";
        }

        // 2. Tokenize输入prompt
        const llama_vocab* vocab = llama_model_get_vocab(model_);
        std::vector<llama_token> tokens = tokenize(vocab, prompt);

        if (tokens.empty()) {
            llama_free(ctx);
            return "[ERROR] Tokenization failed";
        }

        std::cout << "[SimpleInference] Prompt tokens: " << tokens.size() << "\n";

        // 3. 处理prompt（前向传播）
        if (!process_prompt(ctx, tokens)) {
            llama_free(ctx);
            return "[ERROR] Failed to process prompt";
        }

        // 4. 生成tokens
        std::string output = generate_tokens(ctx, vocab, max_tokens);

        // 5. 清理
        llama_free(ctx);

        return output;
    }

private:
    llama_model* model_;

    /**
     * @brief 将文本转换为token序列
     */
    std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
        // 预分配足够的空间（文本长度的4倍通常够用）
        std::vector<llama_token> tokens(text.size() * 4);

        int n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            text.size(),
            tokens.data(),
            tokens.size(),
            true,    // add_special（添加BOS等特殊token）
            false    // parse_special
        );

        if (n_tokens < 0) {
            // 缓冲区不够，重新分配
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

        if (n_tokens > 0) {
            tokens.resize(n_tokens);
        } else {
            tokens.clear();
        }

        return tokens;
    }

    /**
     * @brief 处理prompt tokens（前向传播）
     */
    bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens) {
        // 创建batch
        llama_batch batch = llama_batch_init(tokens.size(), 0, 1);

        // 填充batch
        for (size_t i = 0; i < tokens.size(); ++i) {
            batch.token[i] = tokens[i];
            batch.pos[i] = i;
            batch.seq_id[i][0] = 0;
            batch.n_seq_id[i] = 1;
            // 只需要最后一个位置的logits
            batch.logits[i] = (i == tokens.size() - 1) ? 1 : 0;
        }
        batch.n_tokens = tokens.size();

        // 执行前向传播
        int ret = llama_decode(ctx, batch);
        llama_batch_free(batch);

        return ret == 0;
    }

    /**
     * @brief 生成tokens（贪婪采样）
     */
    std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab, int max_tokens) {
        std::string output;
        output.reserve(max_tokens * 4); // 预分配空间

        const int eos_token = llama_vocab_eos(vocab);
        const int vocab_size = llama_vocab_n_tokens(vocab);

        // 创建用于生成的batch
        llama_batch gen_batch = llama_batch_init(1, 0, 1);
        int n_past = llama_get_kv_cache_used_cells(ctx);

        for (int step = 0; step < max_tokens; ++step) {
            // 获取logits
            const float* logits = llama_get_logits(ctx);
            if (!logits) {
                std::cerr << "[SimpleInference] Failed to get logits\n";
                break;
            }

            // 贪婪采样：选择概率最大的token
            int next_token = 0;
            float max_prob = logits[0];
            for (int i = 1; i < vocab_size; ++i) {
                if (logits[i] > max_prob) {
                    max_prob = logits[i];
                    next_token = i;
                }
            }

            // 检查是否遇到EOS token
            if (next_token == eos_token) {
                std::cout << "[SimpleInference] EOS token reached\n";
                break;
            }

            // 将token转换为文本
            char piece[256] = {0};
            int len = llama_token_to_piece(vocab, next_token, piece, sizeof(piece), 0, false);
            if (len > 0) {
                output.append(piece, len);
            }

            // 准备下一次推理
            gen_batch.n_tokens = 1;
            gen_batch.token[0] = next_token;
            gen_batch.pos[0] = n_past;
            gen_batch.seq_id[0][0] = 0;
            gen_batch.n_seq_id[0] = 1;
            gen_batch.logits[0] = 1;

            // 执行推理
            if (llama_decode(ctx, gen_batch) != 0) {
                std::cerr << "[SimpleInference] Decode failed at step " << step << "\n";
                break;
            }

            n_past++;
        }

        llama_batch_free(gen_batch);

        std::cout << "[SimpleInference] Generated " << output.size() << " bytes\n";
        return output;
    }
};
