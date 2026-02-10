# Server-13 KV Cache 核心技术总结

本文档详细介绍了 Server-13 引入的 **KV Cache（键值缓存）** 技术，原理解析、性能对比以及代码层面的具体改动。

## 1. 核心原理：为什么需要 KV Cache？

### 1.1 痛点：Server-12 的"失忆症"
在 Server-12（以及大多数朴素的推理实现）中，大模型本身是无状态的。为了让它记住之前的对话，我们必须在每一轮对话时，把**所有的历史记录**（Context）重新喂给模型。

*   **第1轮**：处理 `[用户:你好]` -> 生成回复。
*   **第2轮**：处理 `[用户:你好] [AI:您好] [用户:北京在哪]` -> 生成回复。
*   **第3轮**：处理 `[用户:你好] [AI:您好] [用户:北京在哪] [AI:北京是...] [用户:谢谢]` -> 生成回复。

**问题**：前面的 `[用户:你好] [AI:您好]` 在第2轮、第3轮被反复重复计算。随着对话变长，90%以上的计算资源都浪费在"重读旧书"上。

### 1.2 解决方案：KV Cache（阅读书签）
Transformer 模型在处理 Token 时，会计算 Attention 矩阵（Key 和 Value 向量）。**KV Cache** 的思想就是把这些算好的 Key 和 Value 向量**存下来**。

*   **Server-13 的做法**：
    *   **第1轮**：处理 `[用户:你好]` -> 存下 Token 1-10 的 KV 数据。
    *   **第2轮**：系统发现前 10 个 Token 已经算过了，直接从内存读取。**只计算新增的** `[用户:北京在哪]`。

**比喻**：
*   **Server-12**：每次看书都要从第1页读到最新一页，才能看懂剧情。
*   **Server-13**：在书里夹了书签。下次直接翻到书签处继续往下读。

---

## 2. 性能实测对比

我们在 CPU 环境下，使用 `DeepSeek-R1-Distill-Qwen-1.5B` 模型进行了长文本（约 1000 Token）压力测试。

| 测试场景 | Server-12 (无缓存) | Server-13 (KV Cache) | 性能提升 |
| :--- | :--- | :--- | :--- |
| **首轮对话** (Pre-fill) | 31.24s | 31.56s | 持平 (首次都需要全量计算) |
| **次轮对话** (TTFT*) | **15.82s** | **0.42s** | **🚀 37.6倍加速** |

> *TTFT (Time To First Token): 从发送消息到看到第一个字的时间（首字延迟）。

**结论**：
在多轮对话中，Server-13 几乎消除了随着历史增长带来的延迟。无论聊了多久，响应速度始终保持如初。

---

## 3. 代码改动详解

相比于 Server-12，Server-13 在 `src/inference/ModelManager.cpp` 和 `.h` 中做了关键修改。

### 3.1 数据结构升级 (`ModelManager.h`)

**Server-12**:
```cpp
// 每次推理都是临时的
struct ChatSession {
    std::vector<Message> history;
};
```

**Server-13**:
```cpp
struct KVCachedSession {
    ChatSession session;           // 历史记录
    
    // 新增：持久化状态
    struct llama_context* ctx;     // 显存/内存中的上下文句柄
    int n_past;                    // 已处理的 Token 数量
    std::vector<int> cached_tokens;// 用于校验缓存有效性的 Token 序列
    
    // 析构时自动释放巨大的 context 内存
    ~KVCachedSession() { llama_free(ctx); }
};
```

### 3.2 推理逻辑重构 (`ModelManager.cpp`)

**Server-12 (`raw_infer`)**:
1. 创建新的 `llama_context`。
2. 对整个 prompt 进行 Tokenize。
3. 调用 `llama_decode` 处理**所有** Token。
4. 生成回复。
5. 销毁 context。

**Server-13 (`infer` 增量模式)**:
1. 获取现有的 `KVCachedSession`。
2. **缓存校验**：对比当前 Prompt 和 `cached_tokens`，找出最长公共前缀（`n_reuse`）。
3. **增量解码**：
   ```cpp
   // 只解码新增部分，跳过前 n_reuse 个
   int n_to_decode = nTok - n_reuse;
   batch.pos[i] = n_reuse + i; // 告诉模型这些是后续的 Token
   llama_decode(ctx, batch);   // 仅计算新数据
   ```
4. 生成回复。
5. 更新 `n_past` 和 `cached_tokens`，**保留 context** 供下次使用。

### 3.3 关键 Bug 修复：Prune 与缓存失效

**问题描述**：
当对话历史超过限制（如4轮）时，`ChatSession::prune()` 会删除最早的消息并插入 `[summary]`。这导致历史记录发生了**中间篡篡改**，但 KV Cache 以为历史没变，直接追加 Token，导致模型内部状态错乱（报错或乱码）。

**修复代码**：
```cpp
// 1. prune() 现在返回 bool，表示是否修改了历史
bool pruned = session.add("user", msg);

// 2. 如果历史被修改，强制清空缓存
if (pruned) {
    std::cerr << "[KVCache] History pruned. Invalidating cache.\n";
    cached_sess.clearCache(); // 重置 n_past = 0
}
```
这确保了在缓存失效时（如触发上下文限制），系统能自动回退到全量计算，保证稳定性。

---

## 4. 总结

Server-13 展示了工业级 LLM 服务必须具备的核心特性。虽然它增加了内存开销（每个会话占用约 200-500MB 内存来存储 KV Cache），但换来了数量级的延迟降低，是提升用户体验的关键技术。

