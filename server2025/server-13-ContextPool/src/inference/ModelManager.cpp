#include "ModelManager.h"
#include "llama.h"
#include <iostream>
#include <cstring>
#include <algorithm>

// ---------- ctor / dtor ----------
ModelManager::ModelManager()  = default;
ModelManager::~ModelManager() {
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
    if (model_) llama_free_model(model_);
    llama_model_params mp = llama_model_default_params();
    model_ = llama_load_model_from_file(path.c_str(), mp);
    if (!model_) {
        std::cerr << "[Model] load failed: " << path << '\n';
        return false;
    }
    n_ctx_ = n_ctx;
    n_threads_ = n_threads;
    std::cout << "[Model] loaded ok: " << path << '\n';
    return true;
}

// =========== 单轮推理（底层，无历史） ===========
std::string ModelManager::raw_infer(const std::string& prompt, int maxTokens, float temperature) const {
    // 打印 prompt 长度
    std::cerr << "[run] prompt bytes=" << prompt.size() << "  maxTok=" << maxTokens << '\n';

    // 0. 创建新 context（每次推理都新建，线程安全）
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx     = n_ctx_;
    cp.n_threads = n_threads_;
    llama_context* ctx = llama_new_context_with_model(model_, cp);
    if (!ctx) return "[ctx_fail]";

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // 1. tokenize
    std::vector<llama_token> tokBuf(prompt.size() * 4);
    int nTok = llama_tokenize(
        vocab, prompt.c_str(), (int)prompt.size(),
        tokBuf.data(), (int)tokBuf.size(),
        true, false
    );
    if (nTok < 1) { llama_free(ctx); return "[tok_fail]"; }
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
    if (llama_decode(ctx, full) != 0) {
        llama_batch_free(full); llama_free(ctx);
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
    const float* logits = llama_get_logits(ctx);
    if (!logits) break;

    // 贪婪采样（最大概率）
    int   best = 0;
    float bestv = logits[0];
    for (int v = 1; v < vSize; ++v)
        if (logits[v] > bestv) { bestv = logits[v]; best = v; }

    char piece[256] = {0};
    llama_token_to_piece(vocab, best, piece, sizeof(piece), 0, false);
    std::cerr << "[run] step " << step << " tok " << best << " \"" << piece << "\"\n";

    // 先添加到输出
    out += piece;

    // 停止条件：遇到 EOS 或 ChatML 结束标记
    if (best == eos) break;
    // 检查是否出现 ChatML 标记（如 "<|user|>", "<|endoftext|>"）
    if (out.find("<|") != std::string::npos) {
        // 截断到 <| 之前
        size_t pos = out.find("<|");
        out.resize(pos);
        break;
    }

    // 填 batch
    bGen.n_tokens     = 1;
    bGen.token[0]     = best;
    bGen.pos[0]       = nPast;
    bGen.seq_id[0][0] = 0;
    bGen.n_seq_id[0]  = 1;
    bGen.logits[0]    = 1;

    if (llama_decode(ctx, bGen) != 0) break;
    ++nPast;
}

    llama_batch_free(bGen);
    llama_free(ctx);

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

    // 4. 去除结尾多余换行
    while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
        output.pop_back();

    // 5. 保存 assistant 回复
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
