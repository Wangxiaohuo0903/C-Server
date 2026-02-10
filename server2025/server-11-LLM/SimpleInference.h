/*==========================================================
 * SimpleInference -  llama.cpp 单轮推理示例
 *
 * 这份代码演示：
 *  1) 从 GGUF 加载模型（llama_model）
 *  2) （可选）使用模型内置 chat template，把 system/user 消息渲染成最终 prompt
 *  3) 创建推理上下文（llama_context，内部包含 KV cache）
 *  4) tokenize（字符串 -> token id 序列）
 *  5) prefill（把 prompt 全部喂进去，填充 KV cache）
 *  6) decode loop（自回归：取 logits -> 选 token -> 再 decode 1 token）
 *
 * 注意：这是“单轮问答”示例。每次 generate() 都新建并释放 context，
 *      所以不会保留多轮对话历史；要做多轮对话需要复用 context。
 *=========================================================*/

#pragma once
#include "llama.h"
#include <string>
#include <vector>
#include <iostream>

class SimpleInference
{
public:
    // 构造：初始没有模型
    SimpleInference() : model_(nullptr) {}

    // 析构：释放模型（model 是“只读资产”，通常程序结束时释放）
    ~SimpleInference()
    {
        if (model_)
        {
            // 新 API：释放模型对象
            llama_model_free(model_);
        }
    }

    /**
     * @brief 加载 GGUF 格式模型（通常是量化模型）
     *
     * model_ 代表“模型本体”：权重 + 词表 + 元数据（只读）。
     * 这个对象可以被多个 context 复用（多会话/多用户时常见）。
     */
    bool loadModel(const std::string &model_path)
    {
        // 如果之前已经加载过模型，先释放旧模型
        if (model_)
        {
            llama_model_free(model_);
            model_ = nullptr;
        }

        // 取得默认加载参数（mmap、gpu offload 等选项的默认值）
        llama_model_params model_params = llama_model_default_params();

        // macOS Docker 修复: 禁用 mmap，使用内存加载
        // mmap 在 macOS Docker (Apple Silicon) 上可能导致挂起
        model_params.use_mmap = false;  // 禁用 mmap
        model_params.use_mlock = false; // 禁用 mlock

        // macOS Docker 修复: 强制纯CPU模式，避免设备枚举卡住
        model_params.n_gpu_layers = 0; // 不使用GPU
        model_params.main_gpu = -1;    // 强制CPU模式

        std::cout << "[SimpleInference] Loading model: " << model_path << "\n";

        // 从 GGUF 文件加载模型
        model_ = llama_model_load_from_file(model_path.c_str(), model_params);

        if (!model_)
        {
            std::cerr << "[SimpleInference] Failed to load model: " << model_path << "\n";
            return false;
        }

        // ---- 检查模型是否带有 chat template ----
        // chat template 是 GGUF 元数据里的一段模板，用于把对话消息渲染成 prompt 文本。
        // 不同模型的聊天格式不同（role token、分隔符、BOS/EOS 等），模板能避免手写错误。
        const char *tmpl = llama_model_chat_template(model_, nullptr);
        if (tmpl)
        {
            has_chat_template_ = true;
            std::cout << "[SimpleInference] Model has built-in chat template\n";
        }
        else
        {
            has_chat_template_ = false;
            std::cout << "[SimpleInference] Model does not have chat template, will use raw prompt\n";
        }

        std::cout << "[SimpleInference] Model loaded successfully!\n";
        return true;
    }

    /**
     * @brief 单轮文本生成（一次用户输入 -> 一次模型回答）
     *
     * 关键概念：
     *  - prompt：输入文本（可能是“原始文本”或“聊天模板渲染后的文本”）
     *  - context：推理上下文（包含 KV cache；一次对话的运行时状态）
     *  - prefill：把 prompt tokens 全喂进去以建立 KV cache
     *  - decode：之后每生成 1 token 就 decode 1 次，并把 token 写入 KV cache
     *
     * @param prompt 用户输入（如果启用 chat template，会作为 user 消息内容）
     * @param max_tokens 最大生成 token 数（不是字符数）
     * @param use_chat_template 是否使用 chat template（默认 false，兼容纯文本 prompt）
     * @param system_prompt 系统提示词（可选）
     * @return 生成的文本（字符串）
     */
    std::string generate(const std::string &prompt,
                         int max_tokens = 64,
                         bool use_chat_template = true,
                         const std::string &system_prompt = "")
    {
        if (!model_)
        {
            return "[ERROR] Model not loaded";
        }

        // ------------------------------------------------------------
        // Step 1) 构建 full_prompt：决定到底喂给模型的“最终文本”是什么
        // ------------------------------------------------------------
        std::string full_prompt = prompt;

        // 如果调用者希望使用 chat template，且模型确实提供模板：
        //  - 将 system/user 消息数组交给 llama_chat_apply_template 渲染
        if (use_chat_template && has_chat_template_)
        {
            full_prompt = applyChatTemplate(prompt, system_prompt);

            // 若模板应用失败，降级为 raw prompt（保证至少能跑）
            if (full_prompt.empty())
            {
                std::cerr << "[SimpleInference] Failed to apply chat template, using raw prompt\n";
                full_prompt = prompt;
            }
        }

        // ------------------------------------------------------------
        // Step 2) 创建推理上下文 context（最关键：KV cache 在 ctx 里）
        // ------------------------------------------------------------
        llama_context_params ctx_params = llama_context_default_params();

        // n_ctx：上下文长度上限（能记住多少 token）。越大越占内存（KV cache 增大）。
        ctx_params.n_ctx = 2048;

        // n_threads：CPU 推理线程数（通常设为物理核数附近）
        ctx_params.n_threads = 4;

        // n_batch：prefill 阶段一次最多处理多少 token（吞吐相关）。过大可能更占内存或收益不明显。
        ctx_params.n_batch = 512;

        // 基于 model 创建 context（每次 generate 新建 ctx => 单轮问答，不保留历史）
        llama_context *ctx = llama_init_from_model(model_, ctx_params);
        if (!ctx)
        {
            return "[ERROR] Failed to create context";
        }

        // ------------------------------------------------------------
        // Step 3) tokenize：把 full_prompt 转成 token id 序列
        // ------------------------------------------------------------
        const llama_vocab *vocab = llama_model_get_vocab(model_);
        std::vector<llama_token> tokens = tokenize(vocab, full_prompt);

        if (tokens.empty())
        {
            llama_free(ctx);
            return "[ERROR] Tokenization failed";
        }

        std::cout << "[SimpleInference] Prompt tokens: " << tokens.size() << "\n";

        // ------------------------------------------------------------
        // Step 4) prefill：把 prompt tokens 全部喂进去，建立 KV cache
        // ------------------------------------------------------------
        if (!process_prompt(ctx, tokens))
        {
            llama_free(ctx);
            return "[ERROR] Failed to process prompt";
        }

        // ------------------------------------------------------------
        // Step 5) decode loop：自回归生成 tokens
        // ------------------------------------------------------------
        // n_past_init = tokens.size()：表示“历史 token 数”，新生成 token 的 position 从这里开始递增
        std::string output = generate_tokens(ctx, vocab, max_tokens, (int)tokens.size());

        // ------------------------------------------------------------
        // Step 6) 清理：释放 context（本轮推理结束）
        // ------------------------------------------------------------
        llama_free(ctx);

        return output;
    }

private:
    llama_model *model_;
    bool has_chat_template_ = false;

    /**
     * @brief 使用 llama.cpp 内置 chat template 渲染对话为最终 prompt 文本
     *
     * 思路：
     *  1) 构造消息数组 messages：[{role:"system",content:...},{role:"user",content:...}]
     *  2) llama_chat_apply_template() 渲染成一个字符串 full_prompt
     *  3) add_ass=true：在末尾加入 assistant 前缀（让模型知道“该它回答了”）
     *
     * 注意：
     *  - 这里传 tmpl=nullptr，表示“使用模型默认模板”（通常来自 GGUF 元数据）
     *  - buffer 可能不够大，所以做了二次调用扩容
     */
    std::string applyChatTemplate(const std::string &user_message,
                                  const std::string &system_prompt = "")
    {
        // --- 1) 构造消息数组（role + content） ---
        std::vector<llama_chat_message> messages;

        // system 消息（可选）
        if (!system_prompt.empty())
        {
            llama_chat_message sys_msg;
            sys_msg.role = "system";
            sys_msg.content = system_prompt.c_str();
            messages.push_back(sys_msg);
        }

        // user 消息（必选）
        llama_chat_message user_msg;
        user_msg.role = "user";
        user_msg.content = user_message.c_str();
        messages.push_back(user_msg);

        // --- 2) 准备输出缓冲区 ---
        // 经验：渲染后的 prompt 通常会比原始内容更长（包含 role 前缀、分隔符、特殊 token 等）
        // 这里粗略估算为内容字符数 * 2 + 余量
        size_t total_chars = user_message.size() + system_prompt.size();
        std::vector<char> buffer(total_chars * 2 + 1024);

        // --- 3) 调用 chat template 渲染函数 ---
        // 参数解释（以你这段代码的签名为准）：
        //  - tmpl = nullptr：用模型默认模板
        //  - messages + size：消息数组
        //  - add_ass = true：在末尾加 assistant 角色起始标记
        //  - buffer：输出写入目标
        int32_t result = llama_chat_apply_template(
            nullptr, // 使用模型默认 template（如果版本要求 model，请以 llama.h 为准修改）
            messages.data(),
            (int)messages.size(),
            true,
            buffer.data(),
            (int)buffer.size());

        if (result < 0)
        {
            std::cerr << "[SimpleInference] llama_chat_apply_template failed\n";
            return "";
        }

        // 若 result 大于当前 buffer，说明空间不够，需要扩容并重试
        if (result > (int32_t)buffer.size())
        {
            buffer.resize(result + 1);
            result = llama_chat_apply_template(
                nullptr,
                messages.data(),
                (int)messages.size(),
                true,
                buffer.data(),
                (int)buffer.size());
            if (result < 0)
            {
                std::cerr << "[SimpleInference] llama_chat_apply_template failed after resize\n";
                return "";
            }
        }

        std::cout << "[SimpleInference] Chat template applied, formatted prompt size: "
                  << result << " bytes\n";

        // result 是实际写入的字节数（不一定包含 '\0'），因此用 (buffer.data(), result) 构造字符串
        return std::string(buffer.data(), (size_t)result);
    }

    /**
     * @brief tokenize：把 UTF-8 文本转换为 token 序列
     *
     * llama_tokenize 的返回约定：
     *  - 返回 >0：实际 token 数
     *  - 返回 <0：buffer 不够大，所需 token 数为 -ret
     *
     * add_special=true：
     *  - 允许 tokenizer 按模型规则添加特殊 token（如 BOS）
     * parse_special=false：
     *  - 不解析类似 "<|...|>" 的特殊标记为特殊 token（按普通文本处理）
     */
    std::vector<llama_token> tokenize(const llama_vocab *vocab, const std::string &text)
    {
        // 粗略预估：token 数通常比字符数少，但中文/byte-fallback 等情况下可能更复杂
        // 这里用 text.size()*4 做一个偏大的初始 buffer（教学示例里常见做法）
        std::vector<llama_token> tokens(text.size() * 4);

        int n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            (int)text.size(),
            tokens.data(),
            (int)tokens.size(),
            true, // add_special
            false // parse_special
        );

        // 若返回负数 => buffer 不够，扩容到精确大小再来一次
        if (n_tokens < 0)
        {
            tokens.resize((size_t)(-n_tokens));
            n_tokens = llama_tokenize(
                vocab,
                text.c_str(),
                (int)text.size(),
                tokens.data(),
                (int)tokens.size(),
                true,
                false);
        }

        // 调整 vector 大小为实际 token 数
        if (n_tokens > 0)
        {
            tokens.resize((size_t)n_tokens);
        }
        else
        {
            tokens.clear();
        }

        return tokens;
    }

    /**
     * @brief prefill：把 prompt tokens 喂给模型，建立 KV cache
     *
     * llama_batch 的关键字段：
     *  - token[i]：第 i 个 token id
     *  - pos[i]：该 token 在序列中的 position（从 0 递增）
     *  - seq_id / n_seq_id：多序列并行时用；此处固定为单序列 0
     *  - logits[i]：是否需要输出该位置的 logits（1=需要，0=不需要）
     *
     * 这里把 logits 只开在“最后一个 prompt token”：
     *  - 这样 prefill 结束后，ctx 内部就保留了“下一 token 的 logits”
     *  - 生成阶段可以直接用它采样/贪婪选下一个 token
     */
    bool process_prompt(llama_context *ctx, const std::vector<llama_token> &tokens)
    {
        // 初始化一个 batch，容量=prompt token 数
        llama_batch batch = llama_batch_init((int)tokens.size(), 0, 1);

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            batch.token[i] = tokens[i]; // 输入 token
            batch.pos[i] = (int)i;      // position：0..len-1
            batch.seq_id[i][0] = 0;     // 单序列 id=0
            batch.n_seq_id[i] = 1;      // 该 token 属于 1 条序列

            // 只对最后一个 token 请求 logits（节省计算/内存带宽）
            batch.logits[i] = (i == tokens.size() - 1) ? 1 : 0;
        }
        batch.n_tokens = (int)tokens.size();

        // decode：执行一次前向计算，把这些 token 写入 KV cache
        int ret = llama_decode(ctx, batch);

        // 释放 batch 内部资源
        llama_batch_free(batch);

        return ret == 0;
    }

    /**
     * @brief decode loop：自回归生成
     *
     * 输入：
     *  - ctx：已经 prefill 过的上下文（KV cache 已含 prompt）
     *  - n_past_init：历史 token 数（prompt token 数），新 token 的 pos 从这里开始
     *
     * 循环逻辑：
     *  1) 从 ctx 取 logits（下一 token 的分数分布）
     *  2) 选择 next_token（这里用贪婪 argmax）
     *  3) 把 next_token detokenize 成文本片段，拼到 output
     *  4) 将 next_token 作为输入，调用 llama_decode(ctx, batch_of_1)
     *     => 把该 token 写入 KV cache，得到新的 logits
     */
    std::string generate_tokens(llama_context *ctx,
                                const llama_vocab *vocab,
                                int max_tokens,
                                int n_past_init)
    {
        std::string output;
        output.reserve((size_t)max_tokens * 4);

        const int eos_token = llama_vocab_eos(vocab);       // EOS token id
        const int vocab_size = llama_vocab_n_tokens(vocab); // 词表大小

        // 生成阶段每步只喂 1 个 token，因此 batch 容量=1
        llama_batch gen_batch = llama_batch_init(1, 0, 1);

        int n_past = n_past_init; // 当前序列长度/下一 token 的 position 起点

        for (int step = 0; step < max_tokens; ++step)
        {
            // 取出“上一轮 decode”产生的 logits（通常对应最后一个位置）
            const float *logits = llama_get_logits(ctx);
            if (!logits)
            {
                std::cerr << "[SimpleInference] Failed to get logits\n";
                break;
            }

            // --------------------------
            // 采样策略：贪婪采样（argmax）
            // --------------------------
            // logits 是未归一化分数，取最大值即可（softmax 前后 argmax 不变）
            int next_token = 0;
            float max_logit = logits[0];
            for (int i = 1; i < vocab_size; ++i)
            {
                if (logits[i] > max_logit)
                {
                    max_logit = logits[i];
                    next_token = i;
                }
            }

            // 若生成到 EOS，停止
            if (next_token == eos_token)
            {
                std::cout << "[SimpleInference] EOS token reached\n";
                break;
            }

            // --------------------------
            // detokenize：token -> 字符串片段
            // --------------------------
            // 注意：piece buffer 固定 256 是教学简化版；严格做法需处理返回长度>buffer 的情况
            char piece[256] = {0};
            int len = llama_token_to_piece(
                vocab,
                (llama_token)next_token,
                piece,
                (int)sizeof(piece),
                0,    // lstrip：是否去掉前导空格等（取决于版本语义）
                false // special：是否允许输出特殊 token 的文本形式
            );
            if (len > 0)
            {
                output.append(piece, (size_t)len);
            }

            // --------------------------
            // 把新 token 喂回模型：更新 KV cache
            // --------------------------
            gen_batch.n_tokens = 1;
            gen_batch.token[0] = (llama_token)next_token;

            // position 必须递增：n_past 是新 token 的位置
            gen_batch.pos[0] = n_past;

            // 仍然是单序列 id=0
            gen_batch.seq_id[0][0] = 0;
            gen_batch.n_seq_id[0] = 1;

            // 请求 logits：这样下一轮循环能从 ctx 取到新的 logits
            gen_batch.logits[0] = 1;

            // 执行 decode：写入 KV cache，并产生下一 token 的 logits
            if (llama_decode(ctx, gen_batch) != 0)
            {
                std::cerr << "[SimpleInference] Decode failed at step " << step << "\n";
                break;
            }

            // 更新历史长度
            n_past++;
        }

        llama_batch_free(gen_batch);

        std::cout << "[SimpleInference] Generated " << output.size() << " bytes\n";
        return output;
    }
};
