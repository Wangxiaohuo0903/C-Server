#include "ModelManager.h"
#include "llama.h"
#include <iostream>
#include <cstring>
#include <algorithm>

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
std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature) const {
    // 打印 prompt 长度
    std::cerr << "[run] prompt bytes=" << prompt.size() << "  maxTok=" << maxTokens << '\n';

    // 0. 使用持久的 context，清空 KV cache
    if (!ctx_) return "[no_ctx]";

    // 清空 KV cache 以重新开始推理
    llama_kv_cache_clear(ctx_);

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // 1. tokenize
    std::vector<llama_token> tokBuf(prompt.size() * 4);
    int nTok = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.size(),
        tokBuf.data(), (int)tokBuf.size(),
        true, false
    );
    if (nTok < 1) { return "[tok_fail]"; }
    tokBuf.resize(nTok);
    std::cerr << "[run] nTok=" << nTok << '\n';

    // 2. 推 prompt
    llama_batch full = llama_batch_init(nTok, 0, 1);
    for (int i = 0; i < nTok; ++i) {
        full.token[i]     = tokBuf[i];
        full.pos[i]       = i;
        full.seq_id[i][0] = 0;
        full.n_seq_id[i]  = 1;
        full.logits[i]    = (i == nTok - 1);
    }
    full.n_tokens = nTok;
    if (llama_decode(ctx_, full) != 0) {
        llama_batch_free(full);
        return "[decode_prompt_fail]";
    }
    llama_batch_free(full);

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
    std::cerr << "[run] step " << step << " tok " << best << " \"" << piece << "\"\n";

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

    // 3. 生成
    std::string output = raw_infer(prompt, maxTokens, temperature);

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
