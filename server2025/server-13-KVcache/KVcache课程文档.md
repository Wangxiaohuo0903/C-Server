# Server-13-KVcache 课程文档：KV Cache 优化技术详解

## 铺垫概念

### 1. Transformer 架构回顾
- **定义**：Transformer 是一种基于自注意力机制（Self-Attention）的神经网络架构，广泛用于大语言模型（LLM）。
- **核心组件**：
  - **Query（查询）**：当前 token 的查询向量
  - **Key（键）**：历史 tokens 的键向量
  - **Value（值）**：历史 tokens 的值向量
  - **注意力计算**：`Attention(Q, K, V) = softmax(Q * K^T / √d) * V`
- **特点**：每次生成新 token 时，都需要与之前所有 tokens 进行注意力计算。

### 2. 自回归生成（Auto-regressive Generation）
- **定义**：模型逐个生成 token，每次生成依赖于之前所有已生成的 tokens。
- **流程**：
  1. 输入 prompt："你好"
  2. 生成第1个token："世"
  3. 生成第2个token："界"（依赖"你好世"）
  4. 生成第3个token："！"（依赖"你好世界"）
- **问题**：每次生成新 token 都要重新计算之前所有 tokens 的 K 和 V 矩阵。

### 3. 传统推理的性能瓶颈
在没有 KV Cache 的情况下，每次生成都是完整的计算：

```
轮次1：计算 ["你", "好"] 的 K, V → 生成"世"
轮次2：重新计算 ["你", "好", "世"] 的 K, V → 生成"界"
轮次3：重新计算 ["你", "好", "世", "界"] 的 K, V → 生成"！"
```

**性能开销**：
- 假设每个 token 的 K, V 计算耗时 10ms
- 生成 100 个 tokens，总计算次数 = 1 + 2 + 3 + ... + 100 = 5050 次
- 总耗时 ≈ 50秒（极其低效）

---

## 为什么要引入 KV Cache

### 问题分析
在多轮对话场景中：
1. **重复计算**：每次回复都要重新处理整个对话历史
2. **延迟累积**：对话轮次越多，响应越慢
3. **资源浪费**：CPU/GPU 大量时间花费在重复计算上

### 关键观察
在 Transformer 的注意力计算中：
- **K 和 V 矩阵只依赖于 token 本身**，不依赖于后续 tokens
- 一旦某个 token 的 K, V 计算完成，结果就可以缓存复用
- 新 token 的生成只需要计算新 token 的 K, V，然后与缓存的历史 K, V 拼接

### KV Cache 的解决方案
```
轮次1：计算并缓存 ["你", "好"] 的 K, V → 生成"世"
轮次2：复用缓存 + 只计算 ["世"] 的 K, V → 生成"界"
轮次3：复用缓存 + 只计算 ["界"] 的 K, V → 生成"！"
```

**性能提升**：
- 生成 100 个 tokens，总计算次数 = 100 次（每次只计算新 token）
- 总耗时 ≈ 1秒（**50倍性能提升**）

---

## KV Cache 的核心原理

### 1. 注意力计算的数学本质

假设我们已经生成了 n 个 tokens，现在要生成第 n+1 个 token：

**传统方式**（无缓存）：
```
所有 tokens: [t₁, t₂, ..., tₙ]
计算所有 tokens 的 K, V:
  K = [k₁, k₂, ..., kₙ]  # 需要完整计算
  V = [v₁, v₂, ..., vₙ]  # 需要完整计算

新 token tₙ₊₁ 的注意力:
  qₙ₊₁ * [k₁, k₂, ..., kₙ]ᵀ → 注意力权重
  权重 * [v₁, v₂, ..., vₙ] → 输出
```

**KV Cache 方式**：
```
缓存中已有: K_cache = [k₁, k₂, ..., kₙ]
           V_cache = [v₁, v₂, ..., vₙ]

生成 tₙ₊₁ 时:
  1. 只计算新 token 的 kₙ₊₁, vₙ₊₁
  2. 拼接: K_new = concat(K_cache, kₙ₊₁)
         V_new = concat(V_cache, vₙ₊₁)
  3. 计算注意力: qₙ₊₁ * K_newᵀ * V_new
  4. 更新缓存: K_cache = K_new, V_cache = V_new
```

### 2. 内存布局示例

假设模型有 32 层，每层的 K, V 维度是 [头数=32, 序列长度, 头维度=128]：

```cpp
// 单个 token 的 KV Cache 大小
单层 K 大小 = 32 × 128 × sizeof(float16) = 8KB
单层 V 大小 = 32 × 128 × sizeof(float16) = 8KB
单层总计 = 16KB
全部 32 层 = 512KB

// 100 个 tokens 的 KV Cache 大小
总内存 = 100 × 512KB = 50MB
```

**优势**：用 50MB 内存换取 50倍性能提升，性价比极高。

---

## SessionContextPool：KV Cache 的管理器

### 核心数据结构

```cpp
struct SessionContext {
    llama_context* ctx;                // llama.cpp 的推理上下文
    std::vector<llama_token> tokens;   // 已缓存的 token 序列
    int cached_token_count;            // 当前缓存的 token 数量
    std::chrono::steady_clock::time_point last_access;  // 最后访问时间（用于 LRU 淘汰）
};

class SessionContextPool {
private:
    std::unordered_map<std::string, SessionContext> sessions_;  // session_id → context
    llama_model* model_;               // 共享的模型实例
    int max_sessions_;                 // 最大 session 数量
    std::mutex pool_mutex_;            // 保护并发访问
};
```

### 核心方法详解

#### 1. getOrCreateSession - 获取或创建 session

```cpp
SessionContext* SessionContextPool::getOrCreateSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    // 1. 如果 session 已存在，直接返回
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.last_access = std::chrono::steady_clock::now();  // 更新访问时间
        return &it->second;
    }

    // 2. 如果超出容量限制，淘汰最久未使用的 session（LRU）
    if (sessions_.size() >= max_sessions_) {
        evictLRU();
    }

    // 3. 创建新的 context
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;  // 设置最大上下文长度
    llama_context* ctx = llama_new_context_with_model(model_, cp);

    // 4. 初始化 SessionContext 并加入池中
    SessionContext new_ctx;
    new_ctx.ctx = ctx;
    new_ctx.cached_token_count = 0;
    new_ctx.last_access = std::chrono::steady_clock::now();
    sessions_[session_id] = std::move(new_ctx);

    return &sessions_[session_id];
}
```

**设计要点**：
- **复用性**：相同 session_id 复用已有 context
- **容量控制**：通过 LRU 策略防止内存溢出
- **线程安全**：使用 mutex 保护并发访问

#### 2. processIncrementalTokens - 增量处理（核心优化）

这是 KV Cache 最关键的函数，实现增量推理：

```cpp
bool SessionContextPool::processIncrementalTokens(
    const std::string& session_id,
    const std::vector<llama_token>& new_tokens,
    const std::string& prompt_text
) {
    SessionContext* sess = getOrCreateSession(session_id);
    if (!sess) return false;

    // === 关键优化：Common Prefix Detection（公共前缀检测）===
    // 找出新旧 token 序列的公共前缀长度
    int common_prefix_len = 0;
    for (size_t i = 0; i < std::min(sess->tokens.size(), new_tokens.size()); ++i) {
        if (sess->tokens[i] == new_tokens[i]) {
            common_prefix_len++;
        } else {
            break;
        }
    }

    // === 情况1：完全复用（新序列是旧序列的前缀）===
    if (common_prefix_len == new_tokens.size()) {
        // 例如：旧序列 = [1,2,3,4,5]，新序列 = [1,2,3]
        // 不需要任何计算，只需调整缓存长度
        sess->cached_token_count = common_prefix_len;
        return true;
    }

    // === 情况2：部分复用（有公共前缀，但新序列更长）===
    // 例如：旧序列 = [1,2,3]，新序列 = [1,2,3,4,5]
    // 只需计算 [4,5] 的 KV Cache

    // 2.1 移除 KV Cache 中超出公共前缀的部分
    if (common_prefix_len < sess->cached_token_count) {
        llama_kv_cache_seq_rm(sess->ctx, 0, common_prefix_len, -1);
        sess->cached_token_count = common_prefix_len;
    }

    // 2.2 只处理新增的 tokens（增量计算）
    for (size_t i = common_prefix_len; i < new_tokens.size(); ++i) {
        llama_batch batch = llama_batch_init(1, 0, 1);
        batch.token[0] = new_tokens[i];
        batch.pos[0] = (int)i;           // 绝对位置（用于 RoPE 位置编码）
        batch.seq_id[0][0] = 0;           // 序列 ID
        batch.n_seq_id[0] = 1;
        batch.logits[0] = (i == new_tokens.size() - 1);  // 只在最后一个 token 计算 logits
        batch.n_tokens = 1;

        // 执行推理（只计算这一个 token 的 KV）
        if (llama_decode(sess->ctx, batch) != 0) {
            llama_batch_free(batch);
            return false;
        }
        llama_batch_free(batch);
        sess->cached_token_count++;
    }

    // 2.3 更新缓存的 token 序列
    sess->tokens = new_tokens;
    return true;
}
```

**关键优化点**：

1. **公共前缀检测**：
   ```
   旧序列: "你好，我是"     → tokens [1, 2, 3, 4]
   新序列: "你好，我是助手" → tokens [1, 2, 3, 4, 5, 6]
   公共前缀长度 = 4
   只需计算 tokens [5, 6] 的 KV Cache
   ```

2. **位置编码保持一致**：
   ```cpp
   batch.pos[0] = (int)i;  // 使用绝对位置，不是相对位置
   ```
   这确保了 RoPE（旋转位置编码）的正确性。

3. **选择性计算 logits**：
   ```cpp
   batch.logits[0] = (i == new_tokens.size() - 1);
   ```
   只在最后一个 token 计算输出概率，节省计算量。

#### 3. evictLRU - LRU 淘汰策略

```cpp
void SessionContextPool::evictLRU() {
    if (sessions_.empty()) return;

    // 找到最久未使用的 session
    auto oldest = sessions_.begin();
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->second.last_access < oldest->second.last_access) {
            oldest = it;
        }
    }

    // 释放资源
    llama_free(oldest->second.ctx);
    sessions_.erase(oldest);
}
```

**LRU 算法**：
- **定义**：Least Recently Used（最近最少使用）
- **原理**：淘汰最长时间未被访问的项
- **优势**：保留热点数据，提高缓存命中率

---

## 工作流程示例

### 场景：三轮对话

**第1轮：用户说"你好"**
```
1. 用户发送 prompt: "你好"
2. SessionContextPool.getOrCreateSession("session_123")
   → 创建新的 llama_context
3. Tokenize: "你好" → [101, 102]
4. processIncrementalTokens(session_id, [101, 102], "你好")
   → 计算 tokens [101, 102] 的 KV Cache
   → cached_token_count = 2
5. 生成回复: "你好！有什么可以帮助你的吗？"
   → 继续增量计算新 tokens 的 KV Cache
   → cached_token_count = 20（假设回复有 18 个 tokens）
```

**第2轮：用户说"介绍一下自己"**
```
1. 用户发送新 prompt: "介绍一下自己"
2. SessionManager 拼接历史:
   完整 prompt = "你好\n助手：你好！有什么可以帮助你的吗？\n用户：介绍一下自己\n助手："
3. Tokenize: 完整 prompt → [101, 102, 103, ..., 250]（假设 50 个 tokens）
4. processIncrementalTokens(session_id, [101, 102, ..., 250], ...)
   → 检测公共前缀: [101, 102, ..., 120] 已缓存（前 20 个 tokens）
   → 只需计算新增的 30 个 tokens
   → cached_token_count = 50
5. 生成回复: "我是一个 AI 助手..."
   → 继续增量计算
   → cached_token_count = 80
```

**性能对比**：
- **无 KV Cache**：每轮都要重新计算整个对话历史
  - 第1轮：计算 2 tokens
  - 第2轮：计算 50 tokens（**完整重算**）
  - 第3轮：计算 100 tokens（**完整重算**）

- **有 KV Cache**：只计算新增部分
  - 第1轮：计算 2 tokens
  - 第2轮：计算 30 tokens（**复用 20**）
  - 第3轮：计算 20 tokens（**复用 80**）

---

## 重要技术概念

### 1. RoPE 位置编码（Rotary Position Embedding）

**为什么需要位置编码？**
- Transformer 的自注意力机制本身不包含位置信息
- 模型需要知道 tokens 的顺序关系（"猫吃鱼" ≠ "鱼吃猫"）

**RoPE 的核心思想**：
```
对于位置 m 的 token，其查询向量 q 和键向量 k 会被旋转一个角度 θ_m：
q_m = rotate(q, θ_m)
k_m = rotate(k, θ_m)

其中 θ_m 与位置 m 成正比，确保相对位置关系被编码到向量中
```

**在 KV Cache 中的应用**：
```cpp
batch.pos[0] = (int)i;  // 使用绝对位置索引
```
这确保了增量计算时，新 token 的位置编码与完整计算一致。

### 2. llama_batch 数据结构

```cpp
struct llama_batch {
    int32_t n_tokens;           // 当前 batch 中的 token 数量
    llama_token* token;         // token IDs 数组
    int32_t* pos;               // 每个 token 的位置索引（用于 RoPE）
    int32_t** seq_id;           // 每个 token 所属的序列 ID（支持多序列并行）
    int32_t* n_seq_id;          // 每个 token 属于几个序列
    int8_t* logits;             // 是否需要计算该 token 的输出 logits
};
```

**设计目的**：
- 支持单次调用处理多个 tokens
- 支持多序列并行推理（用于批处理）
- 灵活控制哪些 token 需要计算输出

### 3. KV Cache 的内存管理

**llama.cpp 的 KV Cache 接口**：
```cpp
// 删除指定范围的 KV Cache
llama_kv_cache_seq_rm(
    llama_context* ctx,
    int seq_id,      // 序列 ID
    int pos_start,   // 起始位置（包含）
    int pos_end      // 结束位置（不包含，-1 表示到末尾）
);

// 示例：删除位置 10 之后的所有 KV Cache
llama_kv_cache_seq_rm(ctx, 0, 10, -1);
```

**应用场景**：
- 回退操作：删除错误生成的 tokens
- 内存控制：限制 KV Cache 的最大长度
- 多轮对话：删除过时的上下文

---

## 本节课用到的 C++ 知识

### 1. std::unordered_map - 哈希表

**为什么使用哈希表？**
```cpp
std::unordered_map<std::string, SessionContext> sessions_;
```

- **O(1) 查找性能**：根据 session_id 快速找到对应的 context
- **动态大小**：自动扩容，无需预先指定容量
- **键值对存储**：session_id（键）→ SessionContext（值）

**使用示例**：
```cpp
// 插入
sessions_[session_id] = new_context;

// 查找
auto it = sessions_.find(session_id);
if (it != sessions_.end()) {
    // 找到了
    SessionContext& ctx = it->second;
}

// 删除
sessions_.erase(session_id);
```

### 2. std::chrono - 时间库

**LRU 需要记录访问时间**：
```cpp
#include <chrono>

// 记录当前时间点
auto now = std::chrono::steady_clock::now();

// 存储在结构体中
struct SessionContext {
    std::chrono::steady_clock::time_point last_access;
};

// 比较时间点
if (ctx1.last_access < ctx2.last_access) {
    // ctx1 更早被访问
}
```

**为什么用 steady_clock？**
- **单调递增**：不受系统时间调整影响
- **高精度**：适合性能测量和时间比较
- **不可回退**：保证 LRU 逻辑的正确性

### 3. std::vector 的高效操作

**比较两个 vector 的公共前缀**：
```cpp
std::vector<llama_token> old_tokens = {1, 2, 3, 4, 5};
std::vector<llama_token> new_tokens = {1, 2, 3, 6, 7};

int common_prefix_len = 0;
for (size_t i = 0; i < std::min(old_tokens.size(), new_tokens.size()); ++i) {
    if (old_tokens[i] == new_tokens[i]) {
        common_prefix_len++;
    } else {
        break;  // 遇到不同的 token 就停止
    }
}
// 结果：common_prefix_len = 3
```

**性能优化**：
- 使用 `std::min` 避免越界
- 提前 `break` 避免不必要的比较
- `size_t` 类型匹配避免警告

### 4. std::mutex 和线程安全

**为什么需要 mutex？**
```cpp
class SessionContextPool {
private:
    std::unordered_map<std::string, SessionContext> sessions_;
    std::mutex pool_mutex_;  // 保护 sessions_
};

SessionContext* getOrCreateSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(pool_mutex_);  // 自动加锁/解锁
    // ... 访问 sessions_
}
```

**多线程场景**：
```
线程A: getOrCreateSession("user1")  ─┐
                                    ├→ 并发访问 sessions_（数据竞争！）
线程B: getOrCreateSession("user2")  ─┘
```

**解决方案**：
- `std::lock_guard` 在构造时加锁，析构时自动解锁
- 即使函数提前 return，也能保证解锁（RAII 机制）

### 5. std::move 移动语义

**避免不必要的拷贝**：
```cpp
SessionContext new_ctx;
new_ctx.ctx = ctx;
new_ctx.tokens = some_large_vector;  // 假设这是一个很大的 vector

// 错误方式：拷贝（开销大）
sessions_[session_id] = new_ctx;  // 拷贝整个结构体

// 正确方式：移动（开销小）
sessions_[session_id] = std::move(new_ctx);  // 转移所有权，不拷贝
```

**移动 vs 拷贝**：
```
拷贝：
  1. 分配新内存
  2. 逐元素复制数据
  3. 保留原对象

移动：
  1. 转移内部指针
  2. 原对象置空
  3. O(1) 时间复杂度
```

---

## ModelManagerV2 的集成

### 推理接口

```cpp
std::string ModelManagerV2::inferWithCache(
    const std::string& session_id,
    const std::string& prompt,
    int max_tokens,
    float temperature
) {
    if (!model_ || !context_pool_) return "[err]";

    // 1. Tokenize prompt
    std::vector<llama_token> prompt_tokens = tokenizePrompt(prompt);

    // 2. 使用 SessionContextPool 进行增量处理
    if (!context_pool_->processIncrementalTokens(session_id, prompt_tokens, prompt)) {
        return "[decode_fail]";
    }

    // 3. 获取 session 的 context
    SessionContext* sess = context_pool_->getOrCreateSession(session_id);
    if (!sess) return "[ctx_fail]";

    // 4. 生成新 tokens（增量方式）
    std::string generated;
    for (int i = 0; i < max_tokens; ++i) {
        // 4.1 获取最后一个 token 的 logits
        const float* logits = llama_get_logits_ith(sess->ctx, sess->cached_token_count - 1);

        // 4.2 采样下一个 token
        int next_token = sampleToken(logits, temperature);
        if (next_token == eos_token) break;

        // 4.3 转为文本
        std::string token_text = tokenToText(next_token);
        generated += token_text;

        // 4.4 增量添加到 context（复用已有的 KV Cache）
        llama_batch batch = llama_batch_init(1, 0, 1);
        batch.token[0] = next_token;
        batch.pos[0] = sess->cached_token_count;  // 位置 = 当前缓存长度
        batch.seq_id[0][0] = 0;
        batch.n_seq_id[0] = 1;
        batch.logits[0] = true;
        batch.n_tokens = 1;

        llama_decode(sess->ctx, batch);
        llama_batch_free(batch);

        // 4.5 更新缓存状态
        sess->tokens.push_back(next_token);
        sess->cached_token_count++;
    }

    return generated;
}
```

**关键点**：
1. 每次生成只处理 1 个 token（增量）
2. `batch.pos[0] = sess->cached_token_count` 保证位置连续性
3. KV Cache 自动累积，无需手动管理

---

## 性能对比分析

### 场景1：单轮短对话
```
Prompt: "你好"（2 tokens）
生成: 20 tokens

无 KV Cache:
  - Prompt 处理: 2 次 decode
  - 生成阶段: (1+2+3+...+20) = 210 次 decode
  - 总计: 212 次

有 KV Cache:
  - Prompt 处理: 2 次 decode
  - 生成阶段: 20 次 decode（每次只计算 1 个新 token）
  - 总计: 22 次

性能提升: 212 / 22 = 9.6倍
```

### 场景2：多轮长对话
```
第1轮: Prompt 10 tokens, 生成 30 tokens
第2轮: Prompt 60 tokens (含历史), 生成 40 tokens
第3轮: Prompt 120 tokens (含历史), 生成 50 tokens

无 KV Cache:
  - 第1轮: 10 + (1+2+...+30) = 475
  - 第2轮: 60 + (1+2+...+40) = 880
  - 第3轮: 120 + (1+2+...+50) = 1395
  - 总计: 2750 次 decode

有 KV Cache:
  - 第1轮: 10 + 30 = 40
  - 第2轮: 20（新增）+ 40 = 60
  - 第3轮: 10（新增）+ 50 = 60
  - 总计: 160 次 decode

性能提升: 2750 / 160 = 17.2倍
```

**结论**：
- 对话轮次越多，KV Cache 优势越明显
- 内存成本低（~50MB/100 tokens），性能收益高

---

## 应用场景与最佳实践

### 适用场景
1. **多轮对话系统**：客服机器人、个人助手
2. **长文本生成**：文章写作、代码生成
3. **流式输出**：逐 token 返回，用户体验更好
4. **高并发服务**：多用户同时对话，session 隔离

### 不适用场景
1. **单次问答**：无历史依赖，KV Cache 无优势
2. **频繁切换上下文**：LRU 淘汰频繁，缓存命中率低
3. **极长上下文**：超出模型 n_ctx 限制

### 最佳实践
1. **合理设置 max_sessions**：
   ```cpp
   int max_sessions = 并发用户数 × 1.2;  // 留 20% 余量
   ```

2. **监控缓存命中率**：
   ```cpp
   double hit_rate = (total_requests - cache_misses) / total_requests;
   if (hit_rate < 0.7) {
       // 考虑增加 max_sessions 或优化 session 管理策略
   }
   ```

3. **设置合理的 n_ctx**：
   ```cpp
   n_ctx = 4096;  // 平衡内存占用和对话长度
   ```

---

## 总结

### 核心要点
1. **KV Cache 本质**：缓存 Transformer 中间计算结果，避免重复计算
2. **增量推理**：只计算新 tokens，复用历史 KV Cache
3. **SessionContextPool**：管理多用户的 KV Cache，支持并发访问
4. **性能提升**：10-20倍加速，尤其在多轮对话场景

### 技术栈
- llama.cpp API：`llama_batch`, `llama_decode`, `llama_kv_cache_seq_rm`
- C++ 并发编程：`std::mutex`, `std::lock_guard`
- 数据结构：`std::unordered_map`, `std::vector`
- 缓存策略：LRU（Least Recently Used）

### 进阶方向
1. **更复杂的缓存策略**：LFU、ARC 等
2. **分布式 KV Cache**：跨机器共享缓存
3. **量化优化**：INT8/INT4 KV Cache 减少内存占用
4. **Prefix Caching**：系统 prompt 全局共享

---

**课程设计：** 参考 Server-12 多线程课程文档风格
**适用对象：** 理解 C++ 基础、Transformer 架构、多线程编程的开发者
