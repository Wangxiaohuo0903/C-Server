// ModelManager.cpp
//---------------------------------------------
//  ModelManager.cpp  (兼容 llama.cpp v0.2+)
//---------------------------------------------

#include "ModelManager.h"
#include "llama.h"

#include <vector>
#include <string>
#include <mutex>
#include <iostream>
#include <cfloat>

// ---------- ctor / dtor ----------

// 默认构造，延迟初始化 model_/ctx_
ModelManager::ModelManager() = default;

// 析构时，若已分配则释放资源
ModelManager::~ModelManager() {
    if (ctx_) {
        // 释放上下文（也会释放内部的 KV 缓存等）
        llama_free(ctx_);
    }
    if (model_) {
        // 释放模型结构体
        llama_free_model(model_);
    }
}

// ---------- singleton ----------

// 单例访问入口：保证整个进程只有一个 ModelManager 实例
ModelManager& ModelManager::instance() {
    static ModelManager inst;
    return inst;
}

// ---------- loadModel ----------

// 加载指定路径的模型，并创建推理上下文
bool ModelManager::loadModel(const std::string& path,
                             int n_ctx,
                             int n_threads) {
    // 上锁，防止并发调用 loadModel/infer 导致 ctx_、model_ 状态混乱
    std::lock_guard<std::mutex> g(mtx_);

    // 如果之前已加载模型，先释放
    if (ctx_)   { llama_free(ctx_);   ctx_   = nullptr; }
    if (model_) { llama_free_model(model_); model_ = nullptr; }

    // 1) 使用默认模型参数
    llama_model_params mp = llama_model_default_params();
    // 2) 从文件加载模型
    model_ = llama_load_model_from_file(path.c_str(), mp);
    if (!model_) {
        // 返回 false 表示加载失败（文件不存在或格式不对）
        return false;
    }

    // 3) 使用默认上下文参数，并覆盖 n_ctx / n_threads
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx     = n_ctx;      // 最大上下文长度
    cp.n_threads = n_threads;  // 并行线程数
    // 4) 创建新的上下文，该调用会分配 KV 缓存、图计算缓存等
    ctx_ = llama_new_context_with_model(model_, cp);

    // 成功返回 true；ctx_ 为空则为失败
    return ctx_ != nullptr;
}

// ---------- infer (greedy, 无 sampler 链) ----------

std::string ModelManager::infer(const std::string& prompt,
                                int maxTokens,
                                float /*temperature unused*/) {
    // 上锁，保证同一时刻只有一个线程在操作 ctx_
    std::lock_guard<std::mutex> guard(mtx_);

    // 检查模型和上下文是否已正确加载
    if (!ctx_ || !model_) {
        std::cerr << "[infer] model or ctx not initialized\n";
        return "[model not loaded]";
    }

    // 获取词表指针（新版 llama.cpp API）
    const llama_vocab * vocab = llama_model_get_vocab(model_);
    if (!vocab) {
        std::cerr << "[infer] llama_model_get_vocab returned null\n";
        return "[vocab error]";
    }

    // === 1. 分词 (tokenize) ===
    // 预分配一个足够大的 buffer
    std::vector<llama_token> toks(4096);
    int nTok = llama_tokenize(
        vocab,
        prompt.c_str(),                       // 输入文本
        static_cast<int32_t>(prompt.size()),  // 文本长度
        toks.data(),                          // 输出 token buffer
        static_cast<int32_t>(toks.size()),    // buffer 大小
        /*add_special=*/true,                 // 是否添加 bos/eos
        /*parse_special=*/false
    );
    if (nTok <= 0) {
        std::cerr << "[infer] tokenize failed, nTok=" << nTok << '\n';
        return "[tokenize_failed]";
    }
    toks.resize(nTok);  // 裁剪到实际 token 数
    std::cerr << "[infer] tokenized prompt, nTok=" << nTok << '\n';

    // === 2. 推送 prompt 到模型（一次性 batch） ===
    // 初始化 batch：nTok 个 token，n_past=0，序列数=1
    llama_batch batch = llama_batch_init(nTok, /*n_past=*/0, /*n_seq_max=*/1);
    for (int i = 0; i < nTok; ++i) {
        batch.token   [i] = toks[i];  // token id
        batch.pos     [i] = i;        // 位置编码
        batch.seq_id  [i][0] = 0;     // 第 0 条序列
        batch.n_seq_id[i]   = 1;      // 每个 token 属于 1 条序列
        batch.logits [i] = 0;         // 控制要不要输出该 token 的 logits
    }
    // 【trick】只要最后一个 prompt token 输出 logits，用于后续贪心选
    batch.logits[nTok - 1] = 1;
    batch.n_tokens         = nTok;

    // 调用 decode：计算前向图一次性填充 nTok 个 token
    int rc = llama_decode(ctx_, batch);
    std::cerr << "[infer] decode prompt rc=" << rc << '\n';
    llama_batch_free(batch);  // 释放 batch 结构
    if (rc != 0) {
        return "[decode_prompt_failed]";
    }
    std::cerr << "[infer] prompt decode OK\n";

    // === 3. 生成 (generation loop) ===
    int nPast              = nTok;                          // 已经处理过的 token 数
    const llama_token EOS  = llama_vocab_eos(vocab);        // EOS token id
    const int       vocabSize = llama_vocab_n_tokens(vocab);// 词表大小
    std::string     out; out.reserve(maxTokens * 4);        // 结果缓冲

    // 每一步生成一个 token，最多 maxTokens 步
    for (int step = 0; step < maxTokens; ++step) {
        std::cerr << "[infer] gen step=" << step << " nPast=" << nPast << '\n';

        // 3.1 获取当前 logits（指向词表向量）
        const float* logits = llama_get_logits(ctx_);
        if (!logits) {
            std::cerr << "[infer] logits nullptr at step=" << step << '\n';
            break;
        }

        // 3.2 贪心选 max_logits
        llama_token best    = 0;
        float       bestVal = logits[0];
        for (int v = 1; v < vocabSize; ++v) {
            if (logits[v] > bestVal) {
                bestVal = logits[v];
                best    = v;
            }
        }
        std::cerr << "[infer] best token=" << best << '\n';

        // 如果遇到 EOS，则结束
        if (best == EOS) {
            std::cerr << "[infer] hit EOS\n";
            break;
        }

        // 3.3 将 token 转为字符串片段
        char buf[192] = {0};
        int nb = llama_token_to_piece(
            vocab,
            best,
            buf, sizeof(buf),
            /*lstrip=*/0, /*special=*/false
        );
        std::cerr << "[infer] token_to_piece nb=" << nb << '\n';
        if (nb > 0) {
            out.append(buf, nb);
        }

        // 3.4 为下一步生成准备单 token batch
        llama_batch b1 = llama_batch_init(1, nPast, 1);
        b1.embd = nullptr;  // 强制从 token 分支
        // 若 batch_init 未自动分配，则手动 new（兼容旧版）
        if (!b1.token)    b1.token    = new llama_token[1];
        if (!b1.pos)      b1.pos      = new llama_pos  [1];
        if (!b1.seq_id) { 
            b1.seq_id       = new llama_seq_id*[1];
            b1.seq_id[0]    = new llama_seq_id [1];
        }
        if (!b1.n_seq_id) b1.n_seq_id = new int32_t[1];
        if (!b1.logits)   b1.logits   = new int8_t   [1];

        // 填入 best token
        b1.token   [0] = best;
        b1.pos     [0] = nPast;
        b1.seq_id  [0][0] = 0;
        b1.n_seq_id[0] = 1;
        b1.logits  [0] = 1;   // 要输出这一步的 logits
        b1.n_tokens = 1;

        // 3.5 decode 这个新 token
        int rc2 = llama_decode(ctx_, b1);
        std::cerr << "[infer] decode gen rc=" << rc2 << " at step=" << step << '\n';
        llama_batch_free(b1);
        if (rc2 != 0) {
            std::cerr << "[infer] decode gen failed at step=" << step << '\n';
            break;
        }

        ++nPast;
    }

    std::cerr << "[infer] finished, out chars=" << out.size() << '\n';
    return out;
}
