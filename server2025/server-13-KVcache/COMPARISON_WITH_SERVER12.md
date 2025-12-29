# Server-13-KVcache vs Server-12 技术对比文档

## 📋 目录
1. [核心优化目标](#核心优化目标)
2. [架构变化](#架构变化)
3. [代码变更详解](#代码变更详解)
4. [功能增强](#功能增强)
5. [理论知识详解](#理论知识详解)
6. [性能对比](#性能对比)
7. [使用场景](#使用场景)

---

## 核心优化目标

### Server-12
- **设计理念**：每次推理都创建新的 context，完全独立
- **适用场景**：单次问答、无状态请求
- **性能瓶颈**：多轮对话时重复计算之前的 tokens

### Server-13-KVcache
- **设计理念**：复用 KV Cache，增量计算新 tokens
- **适用场景**：多轮对话、上下文延续
- **性能优势**：避免重复计算，3-4倍性能提升

---

## 架构变化

### 1. 新增核心组件

```
server-13-KVcache/
├── include/
│   ├── SessionContextPool.h      [新增] KV Cache 池管理
│   └── ModelManagerV2.h           [重构] 支持 KV Cache
├── src/
│   ├── SessionContextPool.cpp    [新增] 实现 KV Cache 复用逻辑
│   └── ModelManagerV2.cpp         [重构] 集成 SessionContextPool
```

### 2. 架构对比图

**Server-12 架构：**
```
请求 → Router → ModelManager → [创建新 Context] → 推理 → 销毁 Context → 响应
                                      ↓
                               完整计算所有 tokens
```

**Server-13-KVcache 架构：**
```
请求 → Router → ModelManagerV2 → SessionContextPool
                                      ↓
                        [检查是否有可复用 Context]
                                      ↓
                     有 → 复用 + 增量计算新 tokens
                     无 → 创建新 Context → 添加到池中
                                      ↓
                                    响应
```

---

## 代码变更详解

### 1. 新增文件：SessionContextPool.h/cpp

**核心数据结构：**

```cpp
struct SessionContext {
    llama_context* ctx;                    // llama.cpp context 实例
    std::vector<llama_token> tokens;       // 已缓存的 token 序列
    int cached_token_count;                // 当前缓存的 token 数量
    std::chrono::steady_clock::time_point last_access;  // LRU 淘汰用
};

std::unordered_map<std::string, SessionContext> sessions_;  // session_id → context
```

**关键方法：**

```cpp
// 1. 获取或创建 session
SessionContext* getOrCreateSession(const std::string& session_id);

// 2. 增量处理 tokens（核心优化）
bool processIncrementalTokens(
    const std::string& session_id,
    const std::vector<llama_token>& new_tokens,
    const std::string& prompt_text
);

// 3. LRU 淘汰策略
void evictLRU();
```

**增量处理逻辑（Server-13 核心创新）：**

```cpp
bool SessionContextPool::processIncrementalTokens(
    const std::string& session_id,
    const std::vector<llama_token>& new_tokens,
    const std::string& prompt_text
) {
    auto* session_ctx = getOrCreateSession(session_id);

    // 1. 找到公共前缀（已缓存部分）
    int common_prefix = 0;
    while (common_prefix < session_ctx->cached_token_count &&
           common_prefix < new_tokens.size() &&
           session_ctx->tokens[common_prefix] == new_tokens[common_prefix]) {
        common_prefix++;
    }

    // 2. 如果有新 tokens，只计算新的部分
    if (common_prefix < new_tokens.size()) {
        // 删除不匹配的旧 tokens
        llama_kv_cache_seq_rm(session_ctx->ctx, 0, common_prefix, -1);

        // 只推理新增的 tokens
        for (int i = common_prefix; i < new_tokens.size(); ++i) {
            llama_batch batch = llama_batch_init(1, 0, 1);
            batch.token[0] = new_tokens[i];
            batch.pos[0] = i;
            batch.seq_id[0][0] = 0;
            batch.n_seq_id[0] = 1;
            batch.logits[0] = (i == new_tokens.size() - 1);  // 只在最后计算 logits
            batch.n_tokens = 1;

            if (llama_decode(session_ctx->ctx, batch) != 0) {
                llama_batch_free(batch);
                return false;
            }
            llama_batch_free(batch);
        }

        // 3. 更新缓存
        session_ctx->tokens = new_tokens;
        session_ctx->cached_token_count = new_tokens.size();
    }

    return true;
}
```

**理论基础：**
- **KV Cache 机制**：Transformer 中，每个 token 的 Key 和 Value 计算后可以缓存
- **增量计算**：新对话只需计算新 tokens，复用之前的 KV pairs
- **时间复杂度**：O(n) → O(Δn)，其中 Δn 是新增 tokens 数量

---

### 2. 重构：ModelManagerV2.h/cpp

**Server-12 (ModelManager):**
```cpp
class ModelManager {
    llama_model* model_;

    // 每次创建新 context
    std::string raw_infer(const std::string& prompt, int max_tokens, float temp) {
        llama_context* ctx = llama_new_context_with_model(model_, params);
        // ... 推理逻辑
        llama_free(ctx);  // 推理完立即销毁
        return result;
    }
};
```

**Server-13-KVcache (ModelManagerV2):**
```cpp
class ModelManagerV2 {
    llama_model* model_;
    std::unique_ptr<SessionContextPool> session_pool_;  // [新增] KV Cache 池

    // 使用 KV Cache 推理
    std::string inferWithCache(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature
    ) {
        // 1. Tokenize
        std::vector<llama_token> tokens = tokenize(prompt, true);

        // 2. 获取或创建 session context
        auto* session_ctx = session_pool_->getOrCreateSession(session_id);

        // 3. 增量推理（只计算新 tokens）
        session_pool_->processIncrementalTokens(session_id, tokens, prompt);

        // 4. 生成新 tokens（从缓存位置开始）
        return generateTokens(
            session_ctx->ctx,
            max_tokens,
            temperature,
            session_ctx->cached_token_count  // [关键] 从缓存末尾继续生成
        );
    }

    // [新增] 流式推理支持
    std::string inferWithCacheStreaming(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature,
        StreamCallback callback
    );
};
```

**关键变化：**
1. **session_pool_**：管理所有 session 的 KV Cache
2. **inferWithCache()**：替代 raw_infer()，支持增量计算
3. **流式推理**：支持 SSE (Server-Sent Events) 实时返回

---

### 3. Router.h 优化

**Server-12 问题：**
```cpp
// 使用 ChatML 格式，导致模型生成循环标记
std::string context = "<|im_start|>user\n" + user_message + "<|im_end|>\n<|im_start|>assistant\n";
```

**Server-13-KVcache 解决方案：**

```cpp
sessionChatHandler = [&sm, &mm](const HttpRequest& r) {
    // 1. 解析对话历史（最近 3 轮 = 6 条消息）
    std::string history_json = sm.getSessionHistory(session_id);
    std::vector<std::pair<std::string, std::string>> messages;

    // 简单 JSON 解析
    while (pos < history_json.size()) {
        // 提取 "role":"user/assistant" 和 "content":"..."
        messages.push_back({role, content});
    }

    // 2. 构建纯文本格式提示词（避免 ChatML）
    std::string full_prompt;
    int start_idx = std::max(0, (int)messages.size() - 6);
    for (int i = start_idx; i < messages.size(); i++) {
        if (messages[i].first == "user") {
            full_prompt += "User: " + messages[i].second + "\n";
        } else {
            full_prompt += "Assistant: " + messages[i].second + "\n";
        }
    }
    full_prompt += "Assistant:";  // 触发生成

    // 3. 调用 KV Cache 推理
    std::string assistant_reply = mm.inferWithCache(
        session_id, full_prompt, maxTokens, temperature
    );

    // 4. 输出清理（防止生成对话标记）
    size_t user_pos = assistant_reply.find("\nUser:");
    if (user_pos != std::string::npos)
        assistant_reply = assistant_reply.substr(0, user_pos);

    size_t asst_pos = assistant_reply.find("\nAssistant:");
    if (asst_pos != std::string::npos)
        assistant_reply = assistant_reply.substr(0, asst_pos);

    // 清理 DeepSeek thinking 标签
    size_t think_pos = assistant_reply.find("<think>");
    if (think_pos != std::string::npos)
        assistant_reply = assistant_reply.substr(0, think_pos);

    return resp;
};
```

**技术要点：**
1. **放弃 ChatML**：使用简单的 `User: ... \nAssistant: ...` 格式
2. **历史解析**：手动解析 JSON（避免引入 nlohmann/json 依赖）
3. **输出清理**：后处理删除多余的对话标记和思考标签
4. **上下文窗口**：限制为最近 6 条消息（3 轮对话），避免超出 context 长度

---

## 功能增强

### 1. 多轮对话上下文维护

**Server-12：**
- ❌ 每轮对话独立，无法记住之前内容
- ❌ 需要客户端手动管理历史

**Server-13-KVcache：**
- ✅ 自动维护对话历史（SessionManager）
- ✅ KV Cache 自动复用之前的计算结果
- ✅ 最近 3 轮对话自动包含在上下文中

**示例：**
```
Turn 1:
  User: "My name is Alice"
  Assistant: "Nice to meet you, Alice!"
  [KV Cache 缓存了 "User: My name is Alice\nAssistant: Nice to meet you, Alice!\n" 的所有 KV pairs]

Turn 2:
  User: "What's my name?"
  Prompt: "User: My name is Alice\nAssistant: Nice to meet you, Alice!\nUser: What's my name?\nAssistant:"
  [只计算 "User: What's my name?\nAssistant:" 的新 tokens]
  Assistant: "Your name is Alice!"
```

---

### 2. 流式输出 (SSE)

**新增接口：**
```
POST /api/sessions/{session_id}/chat/stream
```

**实现原理：**
```cpp
auto callback = [&](const std::string& token) -> bool {
    // 每生成一个 token 立即发送
    sse_stream << "data: {\"token\":\"" << escapeJson(token) << "\"}\n\n";
    full_reply += token;

    // 检测停止条件
    if (full_reply.find("\nUser:") != std::string::npos) {
        return false;  // 停止生成
    }
    return true;  // 继续生成
};

mm.inferWithCacheStreaming(session_id, full_prompt, maxTokens, temperature, callback);
```

**优势：**
- 用户体验更好（逐字显示，类似 ChatGPT）
- 降低首字延迟（TTFT - Time To First Token）

---

### 3. Session 管理

**新增接口：**
```
POST   /api/sessions/new           - 创建新会话
GET    /api/sessions               - 获取所有会话列表
GET    /api/sessions/{id}/history  - 获取会话历史
DELETE /api/sessions/{id}          - 删除会话
PUT    /api/sessions/{id}/title    - 更新会话标题
```

**SessionManager 数据结构：**
```cpp
struct Message {
    std::string role;      // "user" or "assistant"
    std::string content;
    std::string timestamp;
};

struct Session {
    std::string session_id;
    std::string title;
    std::vector<Message> history;
    std::string created_at;
    std::string updated_at;
};
```

---

## 理论知识详解

### 1. KV Cache 原理

#### 1.1 Transformer 注意力机制

在 Transformer 中，每个 token 的注意力计算需要 Query (Q), Key (K), Value (V)：

```
Attention(Q, K, V) = softmax(Q·K^T / √d_k) · V
```

**关键观察：**
- 对于已生成的 tokens，它们的 K 和 V 是**固定不变**的
- 新 token 只需要计算自己的 Q, K, V，然后与之前缓存的 K, V 做注意力

#### 1.2 KV Cache 示例

假设生成序列 `["Hello", "world", "!"]`：

**无 KV Cache (Server-12):**
```
Step 1: 计算 "Hello" 的 Q, K, V
        Attention(Q_Hello, [K_Hello], [V_Hello])

Step 2: 重新计算 "Hello" 和 "world" 的 Q, K, V
        Attention(Q_world, [K_Hello, K_world], [V_Hello, V_world])

Step 3: 重新计算所有 tokens 的 Q, K, V
        Attention(Q_!, [K_Hello, K_world, K_!], [V_Hello, V_world, V_!])
```
**时间复杂度：** O(n²) - 每步重新计算所有之前的 tokens

**有 KV Cache (Server-13-KVcache):**
```
Step 1: 计算 "Hello" 的 K, V，存入 cache
        Cache: [K_Hello, V_Hello]

Step 2: 只计算 "world" 的 K, V，从 cache 取 "Hello" 的 K, V
        Cache: [K_Hello, V_Hello, K_world, V_world]

Step 3: 只计算 "!" 的 K, V
        Cache: [K_Hello, V_Hello, K_world, V_world, K_!, V_!]
```
**时间复杂度：** O(n) - 每步只计算新 token

#### 1.3 内存与速度权衡

**内存消耗：**
```
KV Cache Size = 2 × n_layers × n_ctx × n_embd × sizeof(float)
              = 2 × 28 × 2048 × 1536 × 4 bytes
              ≈ 700 MB (per session, for DeepSeek-1.5B)
```

**SessionContextPool 的作用：**
- 管理多个 session 的 KV Cache
- LRU 淘汰策略：当 session 数量超过 `max_sessions` (100) 时，淘汰最久未使用的
- 内存总消耗：`700 MB × max_sessions ≈ 70 GB`（实际使用时远低于此）

---

### 2. 增量推理算法

#### 2.1 公共前缀匹配

```cpp
int common_prefix = 0;
while (common_prefix < cached.size() &&
       common_prefix < new_tokens.size() &&
       cached[common_prefix] == new_tokens[common_prefix]) {
    common_prefix++;
}
```

**示例：**
```
Turn 1 tokens: [BOS, "User", ":", "Hello", "\n", "Assistant", ":"]
Turn 2 tokens: [BOS, "User", ":", "Hello", "\n", "Assistant", ":", "Hi", "\n", "User", ":", "How", "?", "\n", "Assistant", ":"]

Common prefix length: 7
需要计算的新 tokens: ["Hi", "\n", "User", ":", "How", "?", "\n", "Assistant", ":"]
```

#### 2.2 KV Cache 删除与追加

```cpp
// 1. 删除不匹配的旧 cache
llama_kv_cache_seq_rm(ctx, seq_id, common_prefix, -1);

// 2. 追加新 tokens 的 KV pairs
for (int i = common_prefix; i < new_tokens.size(); ++i) {
    llama_batch batch;
    batch.token[0] = new_tokens[i];
    batch.pos[0] = i;  // 位置索引
    llama_decode(ctx, batch);  // 计算并自动缓存 KV
}
```

**llama.cpp API 说明：**
- `llama_kv_cache_seq_rm(ctx, seq_id, start, end)`: 删除 [start, end) 范围的 cache
- `batch.pos[0] = i`: 告诉模型这个 token 在序列中的位置（用于位置编码）
- `llama_decode()`: 执行前向传播，自动更新 KV Cache

---

### 3. llama.cpp Context 管理

#### 3.1 Context vs Model

```cpp
llama_model* model;    // 模型权重（共享，只读）
llama_context* ctx;    // 推理状态（每个 session 独立）
```

**Model:**
- 存储模型参数（weights, biases）
- 所有 sessions 共享同一个 model
- 加载一次，常驻内存

**Context:**
- 存储推理状态（KV Cache, 临时激活值）
- 每个 session 独立的 context
- 包含完整的 KV Cache

#### 3.2 Context 参数配置

```cpp
llama_context_params cp = llama_context_default_params();
cp.n_ctx = 2048;        // 上下文窗口大小
cp.n_threads = 4;       // CPU 线程数
cp.n_batch = 512;       // 批处理大小（暂未使用）
cp.rope_freq_base = 10000.0f;  // RoPE 位置编码参数
```

---

### 4. RoPE 位置编码

**Rotary Position Embedding (RoPE)** 是现代 LLM 的位置编码方法。

#### 4.1 原理

传统 Transformer 使用绝对位置编码（加到 embedding 上），RoPE 使用相对位置编码：

```
Q' = RoPE(Q, pos_q)
K' = RoPE(K, pos_k)
Attention ∝ Q'·K'^T = RoPE(Q, pos_q)·RoPE(K, pos_k)^T
```

**关键特性：**
- 注意力分数只依赖于 `pos_q - pos_k`（相对位置）
- 支持外推到更长的序列

#### 4.2 在 KV Cache 中的作用

```cpp
batch.pos[0] = i;  // 告诉模型当前 token 的绝对位置
```

当使用 KV Cache 时，新 token 的位置是 `cached_token_count + step`：
```cpp
int n_past = session_ctx->cached_token_count;
for (int step = 0; step < max_tokens; ++step) {
    batch.pos[0] = n_past + step;  // 正确的位置索引
    llama_decode(ctx, batch);
}
```

如果位置编码错误，模型会产生混乱的输出。

---

## 性能对比

### 1. 延迟对比（多轮对话场景）

假设模型推理速度：20 tokens/s

| 场景 | Server-12 | Server-13-KVcache | 加速比 |
|------|-----------|-------------------|--------|
| **Turn 1** (10 tokens 输入) | 0.5s | 0.5s | 1.0x |
| **Turn 2** (25 tokens 输入) | 1.25s | 0.75s | **1.67x** |
| **Turn 3** (40 tokens 输入) | 2.0s | 0.75s | **2.67x** |
| **Turn 5** (70 tokens 输入) | 3.5s | 0.75s | **4.67x** |

**解释：**
- Server-12：每轮都要重新计算所有 tokens
- Server-13-KVcache：只计算新增的 15 tokens（用户消息 + 触发词）

---

### 2. 内存占用

| 项目 | Server-12 | Server-13-KVcache |
|------|-----------|-------------------|
| **模型权重** | 1.04 GB | 1.04 GB |
| **单次推理临时内存** | ~50 MB | ~50 MB |
| **KV Cache (per session)** | 0 MB | ~700 MB |
| **总内存 (100 sessions)** | 1.04 GB | 1.04 GB + N × 700 MB |

**说明：**
- N = 实际活跃 session 数量（通常远小于 100）
- LRU 淘汰机制确保不会无限增长

---

### 3. 吞吐量对比

**并发场景（10 个并发请求）：**

| 指标 | Server-12 | Server-13-KVcache |
|------|-----------|-------------------|
| **处理方式** | 顺序处理（无并行） | 顺序处理（无并行） |
| **单请求延迟** | 2.0s | 0.8s |
| **总耗时** | 20s | 8s |
| **吞吐量** | 0.5 req/s | 1.25 req/s |

**注意：** Server-13-KVcache 使用单个 context，不支持真正的并行处理。如需并行，应使用 Server-13-Batch。

---

## 使用场景

### Server-12 适用场景
✅ 单次问答（FAQ 系统）
✅ 无状态 API
✅ 简单的文本生成任务
✅ 内存受限环境

### Server-13-KVcache 适用场景
✅ **多轮对话**（客服机器人、助手）
✅ **长文本生成**（文章续写、代码补全）
✅ **交互式应用**（游戏 NPC、教育辅导）
✅ 需要低延迟的实时对话

---

## 总结

### 核心改进点

1. **SessionContextPool**：管理 KV Cache 的生命周期
2. **增量推理**：避免重复计算，3-4倍性能提升
3. **流式输出**：更好的用户体验
4. **多轮上下文**：自动维护对话历史

### 技术亮点

1. **LRU 淘汰**：自动管理内存，防止泄漏
2. **公共前缀优化**：最大化 KV Cache 复用率
3. **纯文本提示词**：避免 ChatML 导致的循环生成
4. **后处理清理**：确保输出质量

### 不足与改进方向

1. **单 context 限制**：无法真正并行处理多个请求（→ 使用 Batch 版本）
2. **内存消耗**：每个 session 占用 ~700 MB（→ 降低 max_sessions 或使用量化）
3. **模型能力**：DeepSeek-1.5B 理解能力有限（→ 更换更大模型）

---

**文档版本：** v1.0
**更新日期：** 2025-12-29
**作者：** AI Infrastructure Team
