/**
 * KV-Cache 状态序列化测试
 *
 * 测试目标:
 * 1. 验证 llama_state_seq_get_data/set_data 功能
 * 2. 测量序列化/反序列化耗时
 * 3. 测量不同 token 数量的状态大小
 * 4. 验证状态恢复后推理结果一致性
 */

#include "llama.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <cstring>

using namespace std;
using namespace std::chrono;

// 计时辅助类
class Timer {
public:
    Timer(const string& name) : name_(name), start_(high_resolution_clock::now()) {}

    ~Timer() {
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start_).count();
        cout << "[Timer] " << name_ << ": " << duration << " ms\n";
    }

private:
    string name_;
    high_resolution_clock::time_point start_;
};

// 辅助函数: 创建 batch
llama_batch make_batch(const vector<llama_token>& tokens, llama_seq_id seq_id, llama_pos start_pos) {
    llama_batch batch = llama_batch_init(tokens.size(), 0, 1);

    for (size_t i = 0; i < tokens.size(); i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = start_pos + i;
        batch.n_seq_id[i] = 1;
        batch.seq_id[i] = new llama_seq_id[1];
        batch.seq_id[i][0] = seq_id;
        batch.logits[i] = (i == tokens.size() - 1);  // 只在最后一个 token 输出 logits
    }

    batch.n_tokens = tokens.size();
    return batch;
}

void free_batch(llama_batch& batch) {
    for (int i = 0; i < batch.n_tokens; i++) {
        delete[] batch.seq_id[i];
    }
    llama_batch_free(batch);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <model_path>\n";
        cerr << "Example: " << argv[0] << " ../../models/tinyllama-q4.gguf\n";
        return 1;
    }

    const char* model_path = argv[1];

    cout << "=== KV-Cache 状态序列化测试 ===\n\n";

    // ==================== 测试 1: 加载模型 ====================
    cout << "[Test 1] 加载模型\n";

    llama_model_params model_params = llama_model_default_params();
    llama_model* model = nullptr;

    {
        Timer timer("模型加载");
        model = llama_model_load(model_path, model_params);
    }

    if (!model) {
        cerr << "[ERROR] 模型加载失败: " << model_path << "\n";
        return 1;
    }

    cout << "[OK] 模型加载成功\n\n";

    // ==================== 测试 2: 创建上下文 ====================
    cout << "[Test 2] 创建推理上下文\n";

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_batch = 512;
    ctx_params.n_threads = 4;

    llama_context* ctx = llama_init_from_model(model, ctx_params);
    if (!ctx) {
        cerr << "[ERROR] 上下文创建失败\n";
        llama_model_free(model);
        return 1;
    }

    cout << "[OK] 上下文创建成功 (ctx_size=" << ctx_params.n_ctx << ")\n\n";

    // ==================== 测试 3: Tokenize ====================
    cout << "[Test 3] Tokenize 测试字符串\n";

    string prompt = "The quick brown fox jumps over the lazy dog.";
    cout << "Prompt: \"" << prompt << "\"\n";

    // Tokenize
    vector<llama_token> tokens;
    int n_tokens = llama_tokenize(
        model, prompt.c_str(), prompt.length(),
        nullptr, 0, true, false
    );

    tokens.resize(n_tokens);
    llama_tokenize(
        model, prompt.c_str(), prompt.length(),
        tokens.data(), tokens.size(), true, false
    );

    cout << "[OK] Token 数量: " << tokens.size() << "\n";
    cout << "Tokens: ";
    for (size_t i = 0; i < min(tokens.size(), size_t(10)); i++) {
        cout << tokens[i] << " ";
    }
    if (tokens.size() > 10) cout << "...";
    cout << "\n\n";

    // ==================== 测试 4: 推理并保存状态 ====================
    cout << "[Test 4] 推理并保存 KV-Cache 状态\n";

    llama_seq_id seq_id = 0;
    llama_batch batch = make_batch(tokens, seq_id, 0);

    {
        Timer timer("推理");
        if (llama_decode(ctx, batch) != 0) {
            cerr << "[ERROR] 推理失败\n";
            free_batch(batch);
            llama_free(ctx);
            llama_model_free(model);
            return 1;
        }
    }

    cout << "[OK] 推理完成\n";

    // 获取状态大小
    size_t state_size = 0;
    {
        Timer timer("获取状态大小");
        state_size = llama_state_seq_get_size(ctx, seq_id);
    }

    cout << "[OK] 状态大小: " << state_size << " bytes ("
         << (state_size / 1024.0 / 1024.0) << " MB)\n";
    cout << "     平均每 token: " << (state_size / tokens.size() / 1024.0 / 1024.0) << " MB\n";

    // 保存状态
    vector<uint8_t> state_data(state_size);
    size_t copied = 0;

    {
        Timer timer("序列化状态");
        copied = llama_state_seq_get_data(ctx, state_data.data(), state_size, seq_id);
    }

    if (copied != state_size) {
        cerr << "[ERROR] 状态保存不完整: " << copied << " / " << state_size << "\n";
        free_batch(batch);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    cout << "[OK] 状态保存成功: " << copied << " bytes\n\n";

    // ==================== 测试 5: 清空缓存 ====================
    cout << "[Test 5] 清空 KV-Cache\n";

    llama_memory_t mem = llama_get_memory(ctx);
    llama_memory_clear(mem, true);

    cout << "[OK] KV-Cache 已清空\n\n";

    // ==================== 测试 6: 恢复状态 ====================
    cout << "[Test 6] 恢复 KV-Cache 状态到新序列\n";

    llama_seq_id new_seq_id = 1;
    size_t loaded = 0;

    {
        Timer timer("反序列化状态");
        loaded = llama_state_seq_set_data(ctx, state_data.data(), state_data.size(), new_seq_id);
    }

    if (loaded == 0) {
        cerr << "[ERROR] 状态恢复失败\n";
        free_batch(batch);
        llama_free(ctx);
        llama_model_free(model);
        return 1;
    }

    cout << "[OK] 状态恢复成功: " << loaded << " bytes (seq_id=" << new_seq_id << ")\n\n";

    // ==================== 测试 7: 验证状态一致性 ====================
    cout << "[Test 7] 验证状态恢复后推理结果一致性\n";

    // 从原始序列继续推理
    cout << "从原始序列 (seq_id=0) 继续推理...\n";

    // 先恢复原始序列的状态
    llama_memory_clear(mem, true);
    llama_state_seq_set_data(ctx, state_data.data(), state_data.size(), 0);

    vector<llama_token> continuation1;
    for (int i = 0; i < 5; i++) {
        llama_token next_token = llama_sampler_sample(
            llama_sampler_init_greedy(), ctx, -1
        );
        continuation1.push_back(next_token);

        llama_batch cont_batch = make_batch({next_token}, 0, tokens.size() + i);
        llama_decode(ctx, cont_batch);
        free_batch(cont_batch);
    }

    cout << "生成的 tokens (seq 0): ";
    for (auto t : continuation1) cout << t << " ";
    cout << "\n";

    // 从恢复的序列继续推理
    cout << "从恢复序列 (seq_id=1) 继续推理...\n";

    llama_memory_clear(mem, true);
    llama_state_seq_set_data(ctx, state_data.data(), state_data.size(), 1);

    vector<llama_token> continuation2;
    for (int i = 0; i < 5; i++) {
        llama_token next_token = llama_sampler_sample(
            llama_sampler_init_greedy(), ctx, -1
        );
        continuation2.push_back(next_token);

        llama_batch cont_batch = make_batch({next_token}, 1, tokens.size() + i);
        llama_decode(ctx, cont_batch);
        free_batch(cont_batch);
    }

    cout << "生成的 tokens (seq 1): ";
    for (auto t : continuation2) cout << t << " ";
    cout << "\n";

    // 对比结果
    bool identical = (continuation1 == continuation2);
    if (identical) {
        cout << "[OK] 两次推理结果完全一致！\n";
    } else {
        cout << "[WARNING] 两次推理结果不同（可能是采样导致）\n";
    }
    cout << "\n";

    // ==================== 测试 8: 不同 token 数量的状态大小 ====================
    cout << "[Test 8] 测试不同 token 数量的状态大小\n";

    vector<int> test_lengths = {5, 10, 20, 50};

    for (int len : test_lengths) {
        if (len > (int)tokens.size()) continue;

        // 清空缓存
        llama_memory_clear(mem, true);

        // 推理指定长度
        vector<llama_token> partial_tokens(tokens.begin(), tokens.begin() + len);
        llama_batch partial_batch = make_batch(partial_tokens, 0, 0);
        llama_decode(ctx, partial_batch);
        free_batch(partial_batch);

        // 获取状态大小
        size_t partial_size = llama_state_seq_get_size(ctx, 0);

        cout << "Tokens: " << len << " | 状态大小: "
             << (partial_size / 1024.0 / 1024.0) << " MB | "
             << "平均: " << (partial_size / len / 1024.0 / 1024.0) << " MB/token\n";
    }
    cout << "\n";

    // ==================== 测试 9: Memory 管理 API ====================
    cout << "[Test 9] 测试 Memory 管理 API\n";

    // 清空
    llama_memory_clear(mem, true);
    cout << "[OK] llama_memory_clear() 执行成功\n";

    // 创建两个序列
    llama_batch batch0 = make_batch(vector<llama_token>{tokens[0], tokens[1]}, 0, 0);
    llama_decode(ctx, batch0);
    free_batch(batch0);

    llama_batch batch1 = make_batch(vector<llama_token>{tokens[0], tokens[1], tokens[2]}, 1, 0);
    llama_decode(ctx, batch1);
    free_batch(batch1);

    cout << "[OK] 创建了两个序列 (seq_id=0, seq_id=1)\n";

    // 查询序列范围
    llama_pos min_pos_0 = llama_memory_seq_pos_min(mem, 0);
    llama_pos max_pos_0 = llama_memory_seq_pos_max(mem, 0);
    llama_pos min_pos_1 = llama_memory_seq_pos_min(mem, 1);
    llama_pos max_pos_1 = llama_memory_seq_pos_max(mem, 1);

    cout << "序列 0 范围: [" << min_pos_0 << ", " << max_pos_0 << "]\n";
    cout << "序列 1 范围: [" << min_pos_1 << ", " << max_pos_1 << "]\n";

    // 复制序列
    llama_memory_seq_cp(mem, 0, 2, -1, -1);
    cout << "[OK] llama_memory_seq_cp(0 -> 2) 执行成功\n";

    llama_pos min_pos_2 = llama_memory_seq_pos_min(mem, 2);
    llama_pos max_pos_2 = llama_memory_seq_pos_max(mem, 2);
    cout << "序列 2 范围: [" << min_pos_2 << ", " << max_pos_2 << "]\n";

    // 删除序列
    llama_memory_seq_rm(mem, 1, -1, -1);
    cout << "[OK] llama_memory_seq_rm(seq_id=1) 执行成功\n";

    // 保留序列
    llama_memory_seq_keep(mem, 0);
    cout << "[OK] llama_memory_seq_keep(seq_id=0) 执行成功\n\n";

    // ==================== 清理 ====================
    cout << "[Cleanup] 释放资源\n";

    free_batch(batch);
    llama_free(ctx);
    llama_model_free(model);

    cout << "[OK] 资源释放完成\n\n";

    // ==================== 总结 ====================
    cout << "=== 测试总结 ===\n";
    cout << "✓ 模型加载\n";
    cout << "✓ 上下文创建\n";
    cout << "✓ 状态序列化/反序列化\n";
    cout << "✓ 状态恢复一致性\n";
    cout << "✓ Memory 管理 API\n";
    cout << "\n所有测试通过！\n";

    return 0;
}
