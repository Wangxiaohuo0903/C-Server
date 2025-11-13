# Day 4: 缓存预热功能实现与测试

**日期**: 2025-11-13
**任务**: 启用并调试缓存预热功能

---

## 一、问题发现与修复

### 1.1 内存错误 (SIGSEGV)

**错误现象**:
```
malloc_consolidate(): unaligned fastbin chunk detected
容器退出代码: 139 (段错误)
```

**根本原因**:

在 `warmupCache()` 函数中，错误地使用 `new` 分配 `batch.seq_id[j]` 数组：

```cpp
// ❌ 错误代码
batch.seq_id[j] = new llama_seq_id[1];  // 覆盖了 llama_batch_init 已分配的指针
batch.seq_id[j][0] = 0;

// 清理时试图 delete 错误的指针
delete[] batch.seq_id[j];
```

`llama_batch_init()` 已经为 `batch.seq_id` 分配了完整的内存，我们不应该再次分配。

**修复方案**:

参考 `raw_infer()` 中的正确用法，直接赋值：

```cpp
// ✅ 正确代码
batch.seq_id[j][0] = 0;  // 直接赋值，不要 new
```

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:469`

**修复提交**: 移除所有 `new llama_seq_id[1]` 和对应的 `delete[]` 调用

---

## 二、缓存预热功能实现

### 2.1 功能设计

预热 5 个常用的 system prompt 模板，在服务启动时自动缓存：

1. `"<|system|>\nYou are a helpful coding assistant.\n"`
2. `"<|system|>\n你是一个编程助手。\n"`
3. `"<|system|>\nYou are a Python expert.\n"`
4. `"<|system|>\n你是一个友好的AI助手。\n"`
5. `"<|system|>\nYou are a helpful assistant.\n"`

### 2.2 实现流程

```cpp
void ModelManager::warmupCache(const std::vector<std::string>& prompts) {
    for (const auto& prompt : prompts) {
        // 1. Tokenize
        auto tokens = tokenize(prompt);

        // 2. 清空序列0的KV cache
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_clear(mem, true);

        // 3. 构造batch并执行forward pass
        llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
        for (size_t j = 0; j < tokens.size(); j++) {
            batch.token[j]     = tokens[j];
            batch.pos[j]       = j;
            batch.seq_id[j][0] = 0;  // 直接赋值
            batch.n_seq_id[j]  = 1;
            batch.logits[j]    = (j == tokens.size() - 1);
        }
        batch.n_tokens = tokens.size();

        llama_decode(ctx_, batch);
        llama_batch_free(batch);

        // 4. 保存到缓存
        savePrefixCache(prompt, tokens.size());
    }
}
```

**关键点**:
- 使用 `llama_memory_clear()` 清空序列0
- 使用 `llama_decode()` 执行前向传播
- 调用 `savePrefixCache()` 保存到 PrefixTree

---

## 三、测试验证

### 3.1 启动日志

```
🔥 ========== Cache Warmup Started ==========
📝 Warming up 5 templates...

[1/5] Processing template:
  📄 Content: <|system|>
You are a helpful coding assistant.
  🔢 Tokens: 14
[PrefixTree] SAVED 14 tokens kv_len=14 seq_id=1 (total=1)
  ✅ Cached: tokens=14

[2/5] Processing template:
  📄 Content: <|system|>
你是一个编程助手。
  🔢 Tokens: 16
[PrefixTree] SAVED 16 tokens kv_len=16 seq_id=2 (total=2)
  ✅ Cached: tokens=16

...

🔥 ========== Cache Warmup Complete ==========
✅ Successfully cached: 5/5 templates
📊 Total cache entries: 5
```

**结果**:
- ✅ 5个模板全部成功缓存
- ✅ 分配序列ID: 1-5
- ✅ 无内存错误，容器正常运行

### 3.2 功能测试

**测试场景**: 三轮对话测试

```bash
# 第一轮: 跳过缓存（需要≥2轮）
curl -X POST http://localhost:8081/infer \
  -d '{"chat_id":"warmup-test-1","prompt":"Write a hello world in Python","max_tokens":50}'

# 第二轮: 保存前缀缓存
curl -X POST http://localhost:8081/infer \
  -d '{"chat_id":"warmup-test-1","prompt":"What about Java?","max_tokens":50}'

# 第三轮: 命中缓存
curl -X POST http://localhost:8081/infer \
  -d '{"chat_id":"warmup-test-1","prompt":"And C++?","max_tokens":50}'
```

**日志输出**:

```
# Round 1
[PrefixExtract] Only 1 round, skipping cache (need ≥2 rounds)

# Round 2
[PrefixExtract] Chat ID: warmup-test-1
[PrefixTree] ✗ MISS for 66 tokens (cache_size=5)
[PrefixTree] SAVED 66 tokens kv_len=66 seq_id=6 (total=6)

# Round 3
[PrefixExtract] Chat ID: warmup-test-1
[PrefixTree] ✓ HIT! seq_id=6 matched_tokens=66/66 (cache_size=6)
[PrefixTree] Current hit rate: 50% (1/2)
[PrefixExtract] ✓ CACHE HIT! Skipping 66 tokens
```

**结果**:
- ✅ 第三轮成功命中缓存（seq_id=6）
- ✅ 跳过 66 个 token 的处理
- ✅ 缓存命中率: 50% (1/2)

---

## 四、架构说明

### 4.1 缓存层级

当前系统有两层缓存：

1. **预热缓存** (seq_id 1-5)
   - 纯 system prompt 模板
   - 服务启动时预计算
   - 理论上可匹配部分前缀

2. **动态缓存** (seq_id 6+)
   - 完整的第一轮对话
   - 包含: system + user + assistant 的完整内容
   - 实际被使用的缓存

### 4.2 前缀提取策略

```cpp
// 提取第一轮完整对话作为前缀
// 从开头到第二个 <|user|> 之前
prefix = prompt.substr(0, second_user_pos);
```

**示例**:
```
<|system|>
You are a helpful coding assistant.
<|user|>
Write a hello world in Python
<|assistant|>
```python
print("Hello, World!")
```
<|user|>     <- 这里是第二个 <|user|>，前面的都是前缀
What about Java?
```

**为什么不直接匹配预热模板？**

- 预热模板只有 system prompt（14-18 tokens）
- 实际前缀包含完整第一轮对话（66+ tokens）
- PrefixTree 支持部分匹配，但当前策略优先完全匹配

**未来优化方向**:
- 可以改进 PrefixTree 的部分匹配策略
- 或者预热完整的常见对话模板

---

## 五、总结

### 5.1 Day 4 完成的工作

1. ✅ **修复内存错误**
   - 诊断并修复 batch 内存管理问题
   - 容器稳定运行，无段错误

2. ✅ **实现缓存预热**
   - 5个模板成功预热
   - 使用正确的 Memory API

3. ✅ **功能验证**
   - 缓存命中测试通过
   - 命中率统计正常

### 5.2 完整功能清单

| 功能 | 状态 | 说明 |
|------|------|------|
| 前缀缓存查找 | ✅ | `findPrefixCache()` |
| 前缀缓存保存 | ✅ | `savePrefixCache()` |
| LRU 缓存淘汰 | ✅ | `evictLRU()` |
| 序列ID回收 | ✅ | `allocateSeqId()` / `releaseSeqId()` |
| 缓存预热 | ✅ | `warmupCache()` |
| 统计信息 | ✅ | `getCacheStats()` / 命中率跟踪 |

### 5.3 性能数据

- **缓存容量**: 32 条目 (MAX_PREFIX_CACHE)
- **预热模板**: 5 个 system prompt
- **命中率**: 初步测试 50%
- **性能提升**: 跳过 66 tokens 的重复计算

### 5.4 代码变更

**核心文件**:
1. `ModelManager.h` - 接口定义
2. `ModelManager.cpp` - 核心逻辑
   - `findPrefixCache()`: 317-355
   - `savePrefixCache()`: 361-395
   - `warmupCache()`: 438-503
3. `PrefixTree.h` - Trie 数据结构
4. `PrefixTree.cpp` - 匹配与淘汰算法

---

## 六、下一步计划

### Day 5 可能的任务:

1. **性能基准测试**
   - 测试不同缓存命中率下的延迟改善
   - 压力测试：并发请求场景

2. **优化前缀匹配策略**
   - 改进 PrefixTree 的部分匹配能力
   - 让预热的 system prompt 能被实际使用

3. **监控与可视化**
   - 添加 Prometheus metrics
   - 缓存命中率实时监控

4. **文档完善**
   - API 使用文档
   - 部署指南

---

**总结**: Day 4 成功修复内存错误并完成缓存预热功能，KV-Cache 前缀缓存系统已经完整可用！
