# 推测式解码系统架构设计

**版本**: v1.0
**日期**: 2025-11-13
**作者**: xiaohuo

---

## 1. 系统总览

### 1.1 架构图

```
┌──────────────────────────────────────────────────────────────────┐
│                        ModelManager                              │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  现有接口（KV-Cache 优化）                                  │ │
│  │  infer(chat_id, user_msg, max_tokens, temperature)          │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  新增接口（推测式解码）                                     │ │
│  │  inferSpeculative(chat_id, user_msg, max_tokens, temp)      │ │
│  └────────────────┬───────────────────────────────────────────┘ │
│                   │                                              │
│  ┌────────────────▼───────────────────────────────────────────┐ │
│  │           SpeculativeDecoder                                │ │
│  │  ┌──────────────────┐       ┌──────────────────┐           │ │
│  │  │  Draft Model     │       │  Target Model    │           │ │
│  │  │  (小模型)        │       │  (大模型)        │           │ │
│  │  │  - TinyLlama-    │       │  - TinyLlama-    │           │ │
│  │  │    160M/500M     │       │    1.1B          │           │ │
│  │  │  - SmolLM-135M   │       │                  │           │ │
│  │  └────────┬─────────┘       └────────┬─────────┘           │ │
│  │           │                           │                     │ │
│  │    genDraft() ──────────────▶  verifyAndAccept()           │ │
│  │    (生成候选)                  (验证并接受)                 │ │
│  └────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

### 1.2 数据流

```
1. 用户请求
   ↓
2. ModelManager 路由
   ├─ infer() ────────────────────▶ 传统 KV-Cache 优化
   └─ inferSpeculative() ─────────▶ 推测式解码
                                    ↓
3. SpeculativeDecoder 主循环
   ┌──────────────────────────────────────────────┐
   │  Loop: while (n_predict < max_tokens)        │
   │    ┌─────────────────────────────────────┐  │
   │    │  (1) Draft 生成                     │  │
   │    │      draft_tokens = genDraft()      │  │
   │    │      小模型生成 N=16 个候选 tokens  │  │
   │    └─────────────────────────────────────┘  │
   │                   ↓                          │
   │    ┌─────────────────────────────────────┐  │
   │    │  (2) Batch 构造                     │  │
   │    │      batch = [last_token] + draft   │  │
   │    └─────────────────────────────────────┘  │
   │                   ↓                          │
   │    ┌─────────────────────────────────────┐  │
   │    │  (3) 大模型验证                     │  │
   │    │      llama_decode(ctx_tgt, batch)   │  │
   │    │      一次性推理 N+1 个 tokens       │  │
   │    └─────────────────────────────────────┘  │
   │                   ↓                          │
   │    ┌─────────────────────────────────────┐  │
   │    │  (4) 采样与接受                     │  │
   │    │      accepted = verifyAndAccept()   │  │
   │    │      逐个验证 draft，遇到不一致停止 │  │
   │    └─────────────────────────────────────┘  │
   │                   ↓                          │
   │    ┌─────────────────────────────────────┐  │
   │    │  (5) 输出 & 更新状态                │  │
   │    │      输出 accepted tokens            │  │
   │    │      n_past += accepted.size()       │  │
   │    │      清理未接受的 KV cache           │  │
   │    └─────────────────────────────────────┘  │
   └──────────────────────────────────────────────┘
                   ↓
4. 返回生成的文本
```

---

## 2. 核心模块设计

### 2.1 SpeculativeDecoder 类

#### 职责

1. **模型管理**：加载和管理 target/draft 两个模型
2. **Draft 生成**：使用小模型快速生成候选 tokens
3. **Batch 验证**：使用大模型并行验证候选 tokens
4. **Token 接受**：根据采样结果决定接受哪些 tokens
5. **统计收集**：记录性能指标（接受率、加速比等）

#### 接口设计

```cpp
class SpeculativeDecoder {
public:
    // 高层接口：文本输入输出
    std::string infer(const std::string& prompt, int max_tokens, float temperature);

    // 中层接口：token 序列输入输出
    std::vector<llama_token> inferTokens(const std::vector<llama_token>& prompt, ...);

    // 低层接口：核心算法
    std::vector<llama_token> genDraft(...);           // Draft 生成
    std::vector<llama_token> verifyAndAccept(...);    // 验证与接受

    // 统计接口
    Stats getStats() const;
    void printStats() const;
};
```

### 2.2 ModelManager 集成

#### 现有架构

```cpp
class ModelManager {
    // 现有成员
    llama_model* model_;
    llama_context* ctx_;
    PrefixTree prefix_tree_;  // KV-Cache 前缀缓存

    // 现有接口
    std::string infer(chat_id, user_msg, max_tokens, temperature);
};
```

#### 扩展设计

```cpp
class ModelManager {
    // 新增成员
    std::unique_ptr<SpeculativeDecoder> spec_decoder_;
    bool enable_speculative_ = false;

    // 新增接口
    std::string inferSpeculative(chat_id, user_msg, max_tokens, temperature);
    bool loadDraftModel(const std::string& draft_model_path);
    void setSpeculativeMode(bool enable);
};
```

#### 配置选择

```cpp
// 在 infer() 中根据配置选择推理方式
std::string ModelManager::infer(...) {
    if (enable_speculative_ && spec_decoder_) {
        return inferSpeculative(...);  // 推测式解码
    } else {
        return inferNormal(...);       // 传统 KV-Cache 优化
    }
}
```

---

## 3. 算法实现细节

### 3.1 genDraft() - Draft 生成

#### 伪代码

```cpp
std::vector<llama_token> SpeculativeDecoder::genDraft(
    const std::vector<llama_token>& prompt,
    llama_token last_token,
    int n_past
) {
    std::vector<llama_token> draft;
    draft.reserve(config_.n_draft);

    // 1. 清空 draft 模型的 KV cache（或复用）
    if (!config_.enable_kv_reuse) {
        llama_memory_t mem = llama_get_memory(ctx_dft_);
        llama_memory_clear(mem, true);
    }

    // 2. 如果需要，先推理 prompt（首次调用）
    if (n_past == 0) {
        llama_batch batch = llama_batch_init(prompt.size(), 0, 1);
        for (size_t i = 0; i < prompt.size(); ++i) {
            batch.token[i] = prompt[i];
            batch.pos[i] = i;
            batch.seq_id[i][0] = 0;
            batch.n_seq_id[i] = 1;
            batch.logits[i] = (i == prompt.size() - 1);
        }
        batch.n_tokens = prompt.size();
        llama_decode(ctx_dft_, batch);
        llama_batch_free(batch);
    }

    // 3. 自回归生成 N 个 draft tokens
    llama_token current = last_token;
    for (int i = 0; i < config_.n_draft; ++i) {
        // 推理当前 token
        llama_batch batch = llama_batch_init(1, 0, 1);
        batch.token[0] = current;
        batch.pos[0] = n_past + i;
        batch.seq_id[0][0] = 0;
        batch.n_seq_id[0] = 1;
        batch.logits[0] = 1;
        batch.n_tokens = 1;

        if (llama_decode(ctx_dft_, batch) != 0) {
            llama_batch_free(batch);
            break;
        }
        llama_batch_free(batch);

        // 采样下一个 token
        llama_token next = sampleToken(ctx_dft_, config_.temperature, config_.top_k, config_.top_p);

        // 置信度检查（可选）
        float prob = getTokenProb(ctx_dft_, next);
        if (prob < config_.p_min) {
            if (config_.verbose) {
                std::cerr << "[Draft] Low confidence " << prob << " < " << config_.p_min << ", stopping\n";
            }
            break;
        }

        draft.push_back(next);
        current = next;

        // EOS 检查
        const llama_vocab* vocab = llama_model_get_vocab(model_dft_);
        if (llama_vocab_is_eog(vocab, next)) {
            break;
        }
    }

    return draft;
}
```

#### 优化点

1. **KV Cache 复用**：如果 draft 模型与 target 模型共享相同的 prompt，可以复用 KV cache
2. **置信度过滤**：当 draft token 概率低于阈值时，停止生成（减少无效 draft）
3. **早停**：遇到 EOS token 时立即停止

### 3.2 verifyAndAccept() - 验证与接受

#### 伪代码

```cpp
std::vector<llama_token> SpeculativeDecoder::verifyAndAccept(
    const std::vector<llama_token>& draft,
    llama_token last_token,
    int n_past
) {
    std::vector<llama_token> accepted;

    // 跳过太短的 draft
    if ((int)draft.size() < config_.n_draft_min) {
        if (config_.verbose) {
            std::cerr << "[Verify] Draft too short (" << draft.size() << " < " << config_.n_draft_min << "), skipping\n";
        }
        return accepted;  // 返回空，fallback 到单步推理
    }

    // 1. 构造 batch：[last_token, draft[0], draft[1], ..., draft[N-1]]
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

    // 2. 大模型一次性验证
    if (llama_decode(ctx_tgt_, batch) != 0) {
        std::cerr << "[Verify] Decode failed\n";
        llama_batch_free(batch);
        return accepted;
    }

    // 3. 逐个采样并验证
    for (size_t i = 0; i < batch_size; ++i) {
        // 获取当前位置的 logits
        const float* logits = llama_get_logits_ith(ctx_tgt_, i);
        if (!logits) break;

        // 采样
        llama_token sampled = sampleToken(ctx_tgt_, config_.temperature, config_.top_k, config_.top_p);

        if (i == 0) {
            // 第一个位置：总是接受采样结果
            accepted.push_back(sampled);
        } else {
            // 验证 draft token
            if (sampled == draft[i - 1]) {
                // 接受
                accepted.push_back(sampled);
                if (config_.verbose) {
                    std::cerr << "[Verify] Accepted draft[" << (i-1) << "]\n";
                }
            } else {
                // 拒绝，使用采样结果并终止
                accepted.push_back(sampled);
                if (config_.verbose) {
                    std::cerr << "[Verify] Rejected draft[" << (i-1) << "], sampled different token\n";
                }
                break;
            }
        }
    }

    llama_batch_free(batch);
    return accepted;
}
```

#### 关键逻辑

1. **Batch 并行验证**：所有 draft tokens 在一个 batch 中并行推理
2. **逐个采样**：对每个位置独立采样，验证是否与 draft 一致
3. **遇到不一致立即停止**：一旦发现不一致，丢弃后续所有 draft tokens

### 3.3 主循环 inferTokens()

```cpp
std::vector<llama_token> SpeculativeDecoder::inferTokens(
    const std::vector<llama_token>& prompt_tokens,
    int max_tokens,
    float temperature
) {
    std::vector<llama_token> result;
    result.reserve(max_tokens);

    // 1. 处理 prompt
    llama_batch batch_prompt = llama_batch_init(prompt_tokens.size() - 1, 0, 1);
    for (size_t i = 0; i < prompt_tokens.size() - 1; ++i) {
        batch_prompt.token[i] = prompt_tokens[i];
        batch_prompt.pos[i] = i;
        batch_prompt.seq_id[i][0] = 0;
        batch_prompt.n_seq_id[i] = 1;
        batch_prompt.logits[i] = (i == prompt_tokens.size() - 2);
    }
    batch_prompt.n_tokens = prompt_tokens.size() - 1;
    llama_decode(ctx_tgt_, batch_prompt);
    llama_batch_free(batch_prompt);

    llama_token last_token = prompt_tokens.back();
    int n_past = prompt_tokens.size() - 1;
    int n_predict = 0;

    const llama_vocab* vocab = llama_model_get_vocab(model_tgt_);

    // 2. 主循环
    while (n_predict < max_tokens) {
        auto t_start = now();

        // (1) Draft 生成
        auto t_draft_start = now();
        std::vector<llama_token> draft = genDraft(prompt_tokens, last_token, n_past);
        auto t_draft_end = now();

        stats_.n_drafted += draft.size();
        stats_.time_draft_ms += elapsedMs(t_draft_start, t_draft_end);

        // (2) Verify 并接受
        auto t_verify_start = now();
        std::vector<llama_token> accepted = verifyAndAccept(draft, last_token, n_past);
        auto t_verify_end = now();

        stats_.time_verify_ms += elapsedMs(t_verify_start, t_verify_end);

        // Fallback: 如果 draft 太短或全部被拒绝，单步推理
        if (accepted.empty()) {
            llama_batch batch_single = llama_batch_init(1, 0, 1);
            batch_single.token[0] = last_token;
            batch_single.pos[0] = n_past;
            batch_single.seq_id[0][0] = 0;
            batch_single.n_seq_id[0] = 1;
            batch_single.logits[0] = 1;
            batch_single.n_tokens = 1;

            llama_decode(ctx_tgt_, batch_single);
            llama_token next = sampleToken(ctx_tgt_, temperature, config_.top_k, config_.top_p);
            llama_batch_free(batch_single);

            accepted.push_back(next);
        }

        // (3) 更新统计
        stats_.n_accepted += (accepted.size() - 1);  // 减去 last_token 的下一个
        stats_.n_predict += accepted.size();

        // (4) 输出并更新状态
        for (size_t i = 0; i < accepted.size(); ++i) {
            result.push_back(accepted[i]);
            last_token = accepted[i];

            if (llama_vocab_is_eog(vocab, last_token)) {
                goto done;  // EOS 检测
            }
        }

        n_past += accepted.size();
        n_predict += accepted.size();

        // (5) 清理未接受的 KV cache
        llama_memory_seq_rm(llama_get_memory(ctx_tgt_), 0, n_past, -1);

        stats_.time_total_ms += elapsedMs(t_start, now());
    }

done:
    // 计算最终统计
    stats_.accept_rate = stats_.n_drafted > 0 ? (double)stats_.n_accepted / stats_.n_drafted : 0.0;
    stats_.speedup = estimateSpeedup();  // 根据接受率估算加速比

    return result;
}
```

---

## 4. 性能优化策略

### 4.1 Draft 生成优化

| 优化点 | 说明 | 预期收益 |
|--------|------|----------|
| **KV Cache 复用** | Draft 模型复用 target 的 prompt KV cache | 减少 30-50% draft 时间 |
| **置信度过滤** | 低置信度时停止 draft，避免无效生成 | 提升 5-10% 接受率 |
| **异步 Draft** | Draft 与 Verify 并行执行（未来） | 理论 2x 加速 |

### 4.2 Batch 构造优化

| 优化点 | 说明 | 预期收益 |
|--------|------|----------|
| **动态 batch 大小** | 根据接受率调整 N | 平衡延迟与吞吐 |
| **Flash Attention** | 使用优化的 attention 实现 | 减少 20-30% 计算时间 |

### 4.3 内存优化

| 优化点 | 说明 | 预期收益 |
|--------|------|----------|
| **共享词汇表** | Target 和 draft 共享 token embedding | 节省 100-200MB 内存 |
| **量化 Draft 模型** | 使用 Q4_K_M 量化 | 节省 50% 内存 |

---

## 5. 与 KV-Cache 前缀缓存的组合

### 5.1 组合策略

```
┌──────────────────────────────────────────────────────────┐
│  用户请求                                                │
│    ↓                                                     │
│  (1) KV-Cache 前缀匹配                                   │
│      - 查找 Prefix Tree                                  │
│      - 加载缓存的 KV state                               │
│      - 跳过前缀计算（如前 50 tokens）                    │
│    ↓                                                     │
│  (2) 推测式解码                                          │
│      - 从 cached 位置开始                                │
│      - Draft 生成 + Verify                               │
│      - 加速后续 token 生成                               │
│    ↓                                                     │
│  输出结果                                                │
└──────────────────────────────────────────────────────────┘
```

### 5.2 实现要点

```cpp
std::string ModelManager::inferSpeculative(...) {
    // 1. 尝试 KV-Cache 前缀匹配
    std::string prefix = extractPrefix(prompt);
    int prefix_kv_len = findPrefixCache(prefix);

    // 2. 构造 prompt（跳过已缓存部分）
    std::vector<llama_token> prompt_tokens = tokenize(prompt);
    std::vector<llama_token> tokens_to_process;
    if (prefix_kv_len > 0) {
        // 跳过前 prefix_kv_len 个 tokens
        tokens_to_process.assign(prompt_tokens.begin() + prefix_kv_len, prompt_tokens.end());
    } else {
        tokens_to_process = prompt_tokens;
    }

    // 3. 推测式解码
    auto result_tokens = spec_decoder_->inferTokens(tokens_to_process, max_tokens, temperature);

    // 4. Detokenize
    return detokenize(result_tokens);
}
```

### 5.3 预期效果

| 场景 | KV-Cache 优化 | 推测式解码 | 组合效果 |
|------|--------------|-----------|---------|
| 多轮对话 Round 1 | 无缓存 | 1.5x 加速 | 1.5x |
| 多轮对话 Round 2+ | 40% 延迟减少 | 1.5x 加速 | 2.5x 加速 |
| 代码生成 | 60% 延迟减少 | 2.0x 加速 | 3.0x 加速 |

**组合加速比 = (1 - 前缀命中率 × 前缀占比) × 推测式加速比**

示例：
- 前缀命中率 80%，前缀占比 50%，推测式 2.0x
- 组合加速 = (1 - 0.8 × 0.5) × 2.0 = 0.6 × 2.0 = **1.2x**（相对于纯推测式）
- 总加速 = 1 / (0.4 + 0.1) = **2.0x**（相对于基线）

---

## 6. HTTP API 设计

### 6.1 配置接口

**POST /config/speculative**

```json
{
    "enable": true,
    "draft_model": "models/tinyllama-160m-q4.gguf",
    "n_draft": 16,
    "n_draft_min": 5,
    "p_min": 0.9,
    "n_threads_draft": 2
}
```

响应：
```json
{
    "status": "success",
    "message": "Speculative decoding enabled"
}
```

### 6.2 推理接口（扩展现有）

**POST /infer**

请求（新增 `use_speculative` 参数）：
```json
{
    "chat_id": "user-123",
    "prompt": "用Python写一个快速排序",
    "max_tokens": 100,
    "temperature": 0.7,
    "use_speculative": true  // 新增
}
```

响应（新增统计信息）：
```json
{
    "answer": "```python\ndef quick_sort(arr):\n    ...",
    "stats": {
        "n_predict": 85,
        "n_drafted": 160,
        "n_accepted": 102,
        "accept_rate": 0.6375,
        "speedup": 2.15,
        "time_ms": 450.5,
        "tokens_per_sec": 188.7
    }
}
```

---

## 7. 测试计划

### 7.1 单元测试

| 测试项 | 测试内容 | 预期结果 |
|--------|---------|---------|
| **Draft 生成** | genDraft() 生成 N=16 个 tokens | 成功生成 1-16 个 tokens |
| **Verify 接受** | verifyAndAccept() 验证 draft | 正确接受/拒绝 draft |
| **EOS 处理** | 生成遇到 EOS token | 正确停止生成 |
| **空 Draft 处理** | draft.size() < n_draft_min | Fallback 到单步推理 |

### 7.2 集成测试

| 测试项 | 测试内容 | 预期结果 |
|--------|---------|---------|
| **端到端推理** | infer() 完整流程 | 生成正确文本 |
| **与 KV-Cache 组合** | 同时启用前缀缓存 | 两种优化都生效 |
| **多轮对话** | 持续对话 3 轮 | 统计信息累积正确 |

### 7.3 性能测试

| 测试项 | 对照组 | 实验组 | 指标 |
|--------|--------|--------|------|
| **代码生成** | 传统自回归 | 推测式解码 | 加速比 > 2.0x |
| **对话问答** | 传统自回归 | 推测式解码 | 加速比 > 1.5x |
| **内存占用** | 单模型 | 双模型 | 增加 < 500MB |

---

## 8. 部署与监控

### 8.1 Docker 配置

```dockerfile
# 在 Dockerfile 中添加 draft 模型下载
RUN wget https://huggingface.co/.../tinyllama-160m-q4.gguf \
    -O /app/models/draft/tinyllama-160m-q4.gguf
```

### 8.2 环境变量

```bash
# .env 文件
ENABLE_SPECULATIVE=true
DRAFT_MODEL_PATH=/app/models/draft/tinyllama-160m-q4.gguf
N_DRAFT=16
N_DRAFT_MIN=5
P_MIN=0.9
```

### 8.3 监控指标

```json
{
    "speculative_stats": {
        "accept_rate": 0.625,
        "speedup": 2.15,
        "n_requests": 150,
        "avg_tokens_per_sec": 185.3
    }
}
```

---

## 9. 风险与挑战

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| **接受率低于预期** | 加速效果不明显 | 调优 draft 模型选择、p_min 阈值 |
| **内存不足** | 无法同时加载两个模型 | 使用更小的 draft 模型（135M） |
| **词汇表不兼容** | 无法使用推测式解码 | 提前验证兼容性，fallback 到单模型 |
| **实现 bug** | 生成错误文本 | 充分测试，与基线对比输出 |

---

## 10. 总结

### 10.1 技术亮点

1. **双模型协同**：小模型 draft + 大模型 verify
2. **Batch 并行验证**：一次推理验证多个 tokens
3. **与 KV-Cache 组合**：两种优化叠加效果
4. **灵活配置**：支持运行时切换推理模式

### 10.2 预期收益

- **性能**：1.5x - 2.5x 加速（取决于场景）
- **成本**：额外内存 < 500MB
- **兼容性**：与现有系统无缝集成

### 10.3 下一步

- ✅ 设计完成
- ⏳ 实现 SpeculativeDecoder.cpp
- ⏳ 集成到 ModelManager
- ⏳ 性能测试与优化

---

**设计版本**: v1.0
**审核状态**: 待审核
**预计实现时间**: 2 周
