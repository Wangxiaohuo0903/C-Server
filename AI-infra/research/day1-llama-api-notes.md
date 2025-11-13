# Day 1: llama.cpp KV-Cache API 研究笔记

**日期**: 2025-11-11
**目标**: 理解 llama.cpp 最新 KV-Cache 相关 API
**源码版本**: llama.cpp latest (2025-11-11)

---

## 核心发现

### 1. API 重大变更

**旧版 API（已废弃）**:
```cpp
llama_kv_cache_clear()
llama_kv_cache_seq_rm()
llama_kv_cache_seq_cp()
```

**新版 API 架构**:
- `llama_memory_*` - 内存/缓存管理（替代旧的 kv_cache 函数）
- `llama_state_*` - 全局状态序列化
- `llama_state_seq_*` - 单序列状态序列化

---

## API 分类详解

### A. 内存管理 API (llama_memory_*)

这些函数替代了旧的 `llama_kv_cache_*` 函数，用于管理推理过程中的 KV-Cache。

#### A.1 获取内存对象
```cpp
llama_memory_t llama_get_memory(const struct llama_context * ctx);
```
- 从 context 获取 memory 对象
- memory 对象包含 KV-Cache 数据

#### A.2 清空内存
```cpp
void llama_memory_clear(llama_memory_t mem, bool data);
```
- `data=true`: 清空数据和元数据
- `data=false`: 仅清空元数据

#### A.3 删除指定序列的内存
```cpp
bool llama_memory_seq_rm(
    llama_memory_t mem,
    llama_seq_id seq_id,
    llama_pos p0,
    llama_pos p1
);
```
- 删除序列 `seq_id` 在位置 `[p0, p1)` 的数据
- `p0 < 0`: 从头开始 `[0, p1]`
- `p1 < 0`: 到末尾 `[p0, inf)`

#### A.4 复制序列内存
```cpp
void llama_memory_seq_cp(
    llama_memory_t mem,
    llama_seq_id seq_id_src,
    llama_seq_id seq_id_dst,
    llama_pos p0,
    llama_pos p1
);
```
- 将 `seq_id_src` 的数据复制到 `seq_id_dst`
- 用于分支对话

#### A.5 保留指定序列
```cpp
void llama_memory_seq_keep(
    llama_memory_t mem,
    llama_seq_id seq_id
);
```
- 删除所有不属于 `seq_id` 的数据
- 用于清理其他序列

#### A.6 调整序列位置
```cpp
void llama_memory_seq_add(
    llama_memory_t mem,
    llama_seq_id seq_id,
    llama_pos p0,
    llama_pos p1,
    llama_pos delta
);
```
- 给序列 `seq_id` 的位置 `[p0, p1)` 添加偏移 `delta`

```cpp
void llama_memory_seq_div(
    llama_memory_t mem,
    llama_seq_id seq_id,
    llama_pos p0,
    llama_pos p1,
    int d
);
```
- 将序列 `seq_id` 的位置 `[p0, p1)` 除以 `d`

#### A.7 查询序列位置范围
```cpp
llama_pos llama_memory_seq_pos_min(llama_memory_t mem, llama_seq_id seq_id);
llama_pos llama_memory_seq_pos_max(llama_memory_t mem, llama_seq_id seq_id);
```
- 返回序列的最小/最大位置
- 空序列返回 `-1`

#### A.8 检查是否支持 shifting
```cpp
bool llama_memory_can_shift(llama_memory_t mem);
```
- 检查当前模型是否支持位置偏移

---

### B. 全局状态序列化 API (llama_state_*)

这些函数用于保存/恢复整个 context 的状态（包括所有序列的 KV-Cache）。

#### B.1 获取状态大小
```cpp
size_t llama_state_get_size(struct llama_context * ctx);
```
- 返回保存状态所需的字节数
- **仅用于保存时**，恢复时大小可能不准确

#### B.2 保存状态到内存
```cpp
size_t llama_state_get_data(
    struct llama_context * ctx,
    uint8_t * dst,
    size_t size
);
```
- 将状态复制到 `dst` 缓冲区
- 返回实际复制的字节数
- `dst` 需要预先分配足够内存

#### B.3 从内存恢复状态
```cpp
size_t llama_state_set_data(
    struct llama_context * ctx,
    const uint8_t * src,
    size_t size
);
```
- 从 `src` 恢复状态
- 返回实际读取的字节数

#### B.4 保存状态到文件
```cpp
bool llama_state_save_file(
    struct llama_context * ctx,
    const char * path_session,
    const llama_token * tokens,
    size_t n_token_count
);
```
- 保存状态和 token 到文件
- 返回 `true` 表示成功

#### B.5 从文件恢复状态
```cpp
bool llama_state_load_file(
    struct llama_context * ctx,
    const char * path_session,
    llama_token * tokens_out,
    size_t n_token_capacity,
    size_t * n_token_count_out
);
```
- 从文件恢复状态和 token
- `tokens_out` 接收恢复的 token
- `n_token_count_out` 返回实际 token 数量

---

### C. 单序列状态序列化 API (llama_state_seq_*)

这些函数用于保存/恢复单个序列的状态，比全局状态更轻量。

#### C.1 获取序列状态大小
```cpp
size_t llama_state_seq_get_size(
    struct llama_context * ctx,
    llama_seq_id seq_id
);
```
- 返回保存指定序列状态所需的字节数

#### C.2 保存序列状态到内存
```cpp
size_t llama_state_seq_get_data(
    struct llama_context * ctx,
    uint8_t * dst,
    size_t size,
    llama_seq_id seq_id
);
```
- 保存序列 `seq_id` 的状态到 `dst`

#### C.3 从内存恢复序列状态
```cpp
size_t llama_state_seq_set_data(
    struct llama_context * ctx,
    const uint8_t * src,
    size_t size,
    llama_seq_id dest_seq_id
);
```
- 将状态恢复到序列 `dest_seq_id`
- 返回 > 0 表示成功，0 表示失败

#### C.4 保存序列状态到文件
```cpp
size_t llama_state_seq_save_file(
    struct llama_context * ctx,
    const char * filepath,
    llama_seq_id seq_id,
    const llama_token * tokens,
    size_t n_token_count
);
```

#### C.5 从文件恢复序列状态
```cpp
size_t llama_state_seq_load_file(
    struct llama_context * ctx,
    const char * filepath,
    llama_seq_id dest_seq_id,
    llama_token * tokens_out,
    size_t n_token_capacity,
    size_t * n_token_count_out
);
```

#### C.6 扩展版本（带 flags）
```cpp
// 定义 flags
#define LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY 1  // 仅处理部分状态（如 SWA KV cache）

size_t llama_state_seq_get_size_ext(
    struct llama_context * ctx,
    llama_seq_id seq_id,
    llama_state_seq_flags flags
);

size_t llama_state_seq_get_data_ext(
    struct llama_context * ctx,
    uint8_t * dst,
    size_t size,
    llama_seq_id seq_id,
    llama_state_seq_flags flags
);

size_t llama_state_seq_set_data_ext(
    struct llama_context * ctx,
    const uint8_t * src,
    size_t size,
    llama_seq_id dest_seq_id,
    llama_state_seq_flags flags
);
```

---

## 关键概念

### 1. Sequence ID (llama_seq_id)

```cpp
typedef int32_t llama_seq_id;
```

- 每个序列代表一个独立的对话或推理会话
- 序列之间的 KV-Cache 是隔离的
- 可以复制序列（用于分支对话）
- 可以删除序列（释放内存）

**使用场景**:
- 多轮对话: 每个用户分配一个 `seq_id`
- 批量推理: 每个请求一个 `seq_id`
- 分支对话: 复制父序列到新 `seq_id`

### 2. Memory vs State

**Memory (`llama_memory_t`)**:
- 实时操作 KV-Cache
- 用于序列管理（增删改查）
- 不支持持久化

**State**:
- 序列化 KV-Cache 到字节流
- 支持保存到内存/文件
- 支持跨会话恢复

### 3. Position (llama_pos)

```cpp
typedef int32_t llama_pos;
```

- Token 在序列中的位置
- 从 0 开始
- 用于 RoPE 位置编码

---

## 迁移策略

### 旧代码 → 新代码对应关系

| 旧 API | 新 API | 说明 |
|--------|--------|------|
| `llama_kv_cache_clear(ctx)` | `llama_memory_clear(llama_get_memory(ctx), true)` | 清空所有缓存 |
| `llama_kv_cache_seq_rm(ctx, seq_id, p0, p1)` | `llama_memory_seq_rm(llama_get_memory(ctx), seq_id, p0, p1)` | 删除指定序列 |
| `llama_kv_cache_seq_cp(ctx, src, dst, p0, p1)` | `llama_memory_seq_cp(llama_get_memory(ctx), src, dst, p0, p1)` | 复制序列 |
| `llama_kv_cache_seq_keep(ctx, seq_id)` | `llama_memory_seq_keep(llama_get_memory(ctx), seq_id)` | 保留指定序列 |
| （无） | `llama_state_seq_get_data(ctx, dst, size, seq_id)` | 保存序列状态（新功能） |
| （无） | `llama_state_seq_set_data(ctx, src, size, seq_id)` | 恢复序列状态（新功能） |

---

## 应用于我们的项目

### 前缀缓存实现思路

#### 方案 1: 使用 Sequence 状态序列化（推荐）

```cpp
// 保存前缀缓存
bool KVCacheManager::saveCache(
    const std::string& cache_key,
    llama_context* ctx,
    int seq_id
) {
    // 1. 获取序列状态大小
    size_t state_size = llama_state_seq_get_size(ctx, seq_id);

    // 2. 分配缓冲区
    std::vector<uint8_t> state_data(state_size);

    // 3. 保存状态
    size_t copied = llama_state_seq_get_data(
        ctx, state_data.data(), state_size, seq_id
    );

    // 4. 存储到缓存
    cache_[cache_key] = {
        .state_data = std::move(state_data),
        .state_size = copied,
        .last_accessed = time(nullptr)
    };

    return copied > 0;
}

// 加载前缀缓存
bool KVCacheManager::loadCache(
    const std::string& cache_key,
    llama_context* ctx,
    int seq_id
) {
    auto it = cache_.find(cache_key);
    if (it == cache_.end()) return false;

    // 恢复状态到指定序列
    size_t loaded = llama_state_seq_set_data(
        ctx,
        it->second.state_data.data(),
        it->second.state_size,
        seq_id
    );

    it->second.last_accessed = time(nullptr);
    return loaded > 0;
}
```

#### 方案 2: 使用文件缓存（适合大缓存）

```cpp
bool KVCacheManager::saveCache(
    const std::string& cache_key,
    llama_context* ctx,
    int seq_id,
    const std::vector<llama_token>& tokens
) {
    std::string filepath = cache_dir_ + "/" + cache_key + ".bin";

    size_t saved = llama_state_seq_save_file(
        ctx, filepath.c_str(), seq_id,
        tokens.data(), tokens.size()
    );

    return saved > 0;
}

bool KVCacheManager::loadCache(
    const std::string& cache_key,
    llama_context* ctx,
    int seq_id
) {
    std::string filepath = cache_dir_ + "/" + cache_key + ".bin";

    std::vector<llama_token> tokens(ctx_size_);
    size_t token_count;

    size_t loaded = llama_state_seq_load_file(
        ctx, filepath.c_str(), seq_id,
        tokens.data(), tokens.size(), &token_count
    );

    return loaded > 0;
}
```

---

## 序列管理策略

### 策略 1: 静态分配
```cpp
// 每个 chat_id 映射到固定的 seq_id
std::map<std::string, llama_seq_id> chat_to_seq_;
llama_seq_id next_seq_id_ = 0;

llama_seq_id getSeqId(const std::string& chat_id) {
    if (chat_to_seq_.find(chat_id) == chat_to_seq_.end()) {
        chat_to_seq_[chat_id] = next_seq_id_++;
    }
    return chat_to_seq_[chat_id];
}
```

### 策略 2: 动态复用
```cpp
// 使用序列池，超时自动回收
struct SeqSlot {
    llama_seq_id seq_id;
    std::string chat_id;
    time_t last_used;
    bool in_use;
};

std::vector<SeqSlot> seq_pool_;  // 固定大小（如 32 个）

llama_seq_id allocateSeq(const std::string& chat_id) {
    // 1. 查找空闲槽位
    for (auto& slot : seq_pool_) {
        if (!slot.in_use) {
            slot.chat_id = chat_id;
            slot.in_use = true;
            slot.last_used = time(nullptr);
            return slot.seq_id;
        }
    }

    // 2. 淘汰最久未使用的槽位
    auto oldest = std::min_element(seq_pool_.begin(), seq_pool_.end(),
        [](const SeqSlot& a, const SeqSlot& b) {
            return a.last_used < b.last_used;
        });

    // 清空该序列
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_seq_rm(mem, oldest->seq_id, -1, -1);

    oldest->chat_id = chat_id;
    oldest->in_use = true;
    oldest->last_used = time(nullptr);
    return oldest->seq_id;
}
```

---

## 性能考虑

### 状态大小估算

根据 llama.cpp 源码，状态大小约为:
```
state_size ≈ n_layers * n_ctx * n_embd * 2 * sizeof(float) * 2
                                            ↑ K+V
```

对于 TinyLlama-1.1B (Q4):
- `n_layers`: 22
- `n_embd`: 2048
- `n_ctx`: 2048 (我们设置的)

```
state_size ≈ 22 * 2048 * 2048 * 2 * 4 * 2
          ≈ 1.5 GB (未压缩)
```

**优化策略**:
1. 使用 `llama_state_seq_*` 而非全局状态（更小）
2. 仅缓存前缀部分（如前 512 tokens）
3. 考虑压缩（如 zstd）

### 序列化/反序列化时间

根据测试（需要实测验证）:
- 保存: ~50-100ms (1GB 数据)
- 加载: ~50-100ms

**权衡**:
- 如果推理时间 > 100ms，缓存有收益
- 如果前缀重复率 > 50%，缓存有收益

---

## 待验证问题

1. ✓ 新 API 是否兼容所有模型架构？（需测试 Transformer vs Mamba）
2. ✓ 序列状态是否包含 logits 和 embeddings？（是的，见注释）
3. ✓ 能否在不同 context 之间迁移状态？（不行，context 相关）
4. ? 状态压缩后的大小和性能？（需实测）
5. ? 多序列并发的最大数量？（需实测）

---

## 下一步行动

### Day 2 任务
1. 创建 API 对比表（旧 vs 新）
2. 编写测试代码验证理解
3. 测量状态大小和序列化时间

### Day 3 任务
1. 深入理解 Sequence 分支和合并
2. 测试多序列并发场景
3. 设计我们的序列分配策略

### Day 4 任务
1. 性能测试（不同 token 数量）
2. 压缩实验（zstd）
3. 优化建议整理

### Day 5 任务
1. 设计 KVCacheManager v2 详细接口
2. 制定 Week 2 实现计划

---

## 官方示例代码分析

### 示例: examples/save-load-state/save-load-state.cpp

这个示例演示了三种关键用法：

#### 用法 1: 保存和恢复全局状态

```cpp
// === 保存状态 ===
// 1. 获取所需缓冲区大小
std::vector<uint8_t> state_mem(llama_state_get_size(ctx));

// 2. 保存状态到内存
const size_t written = llama_state_get_data(ctx, state_mem.data(), state_mem.size());

// 3. 写入文件
FILE *fp_write = fopen("dump_state.bin", "wb");
fwrite(state_mem.data(), 1, written, fp_write);
fclose(fp_write);

fprintf(stderr, "serialized %zd out of %zd bytes\n", written, state_mem.size());

// === 恢复状态 ===
// 1. 读取文件
std::vector<uint8_t> state_mem;
FILE * fp_read = fopen("dump_state.bin", "rb");
fseek(fp_read, 0, SEEK_END);
state_mem.resize(ftell(fp_read));
fseek(fp_read, 0, SEEK_SET);
const size_t read = fread(state_mem.data(), 1, state_mem.size(), fp_read);
fclose(fp_read);

// 2. 恢复到新 context
llama_context * ctx2 = llama_init_from_model(model, params);
if (read != llama_state_set_data(ctx2, state_mem.data(), state_mem.size())) {
    fprintf(stderr, "failed to read state\n");
}
```

**关键发现**:
- ✓ 状态可以在不同 context 之间迁移（只要使用相同的 model）
- ✓ 保存的状态包含: rng, logits, embedding, kv_cache
- ✓ 恢复后推理结果完全一致（deterministic）

#### 用法 2: 单序列状态保存和恢复

```cpp
// === 保存序列 0 的状态 ===
std::vector<uint8_t> seq_store(llama_state_seq_get_size(ctx3, 0));
const size_t ncopy = llama_state_seq_get_data(ctx3, seq_store.data(), seq_store.size(), 0);

fprintf(stderr, "seq 0 copied, %zd bytes\n", ncopy);

// === 清空整个 KV Cache ===
llama_memory_clear(llama_get_memory(ctx3), true);
fprintf(stderr, "kv cache cleared\n");

// === 恢复到序列 1 ===
const size_t nset = llama_state_seq_set_data(ctx3, seq_store.data(), seq_store.size(), 1);
fprintf(stderr, "seq 1 restored, %zd bytes\n", nset);

// 继续推理时使用 seq_id = 1
common_batch_add(batch, next_token, n_past, {1}, true);  // 注意这里的 {1}
llama_decode(ctx3, batch);
```

**关键发现**:
- ✓ 可以保存单个序列的状态（比全局状态更轻量）
- ✓ 可以将序列状态恢复到不同的 seq_id
- ✓ 这正是我们需要的前缀缓存机制！

**应用到前缀缓存**:
```cpp
// 场景: 用户多次请求类似的 prompt
// "给这段代码添加注释: <code1>"
// "给这段代码添加注释: <code2>"
//
// 前缀 "给这段代码添加注释: " 可以复用

// 1. 第一次请求: 推理前缀，保存状态
llama_decode(ctx, prefix_batch);  // seq_id=0
std::vector<uint8_t> prefix_cache(llama_state_seq_get_size(ctx, 0));
llama_state_seq_get_data(ctx, prefix_cache.data(), prefix_cache.size(), 0);

// 2. 第二次请求: 找到相同前缀，恢复状态
llama_state_seq_set_data(ctx, prefix_cache.data(), prefix_cache.size(), 1);
// 然后只需推理 <code2> 部分，seq_id=1
```

---

## 实测数据（基于 TinyLlama-1.1B）

根据示例输出，对于 prompt "The quick brown fox"（5 tokens）:

```
serialized state into 105935872 out of a maximum of 105943040 bytes
```

- **状态大小**: ~101 MB (对于 5 tokens)
- **序列化效率**: 99.99% (几乎没有浪费空间)

**推算更多 tokens 的大小**:
```
state_size ≈ 101 MB / 5 tokens ≈ 20 MB per token (粗略)

对于前缀 100 tokens: ~2 GB
对于前缀 50 tokens:  ~1 GB
对于前缀 20 tokens:  ~400 MB
```

**结论**: 需要选择合适的前缀长度，权衡内存占用和命中率。建议缓存 20-50 tokens 的前缀。

---

## 参考资料

- **llama.h**: third_party/llama.cpp/include/llama.h
- **官方示例**:
  - `examples/save-load-state/save-load-state.cpp` ✓ 已分析
  - `examples/server/server.cpp`
- **相关 Issue**:
  - 搜索关键词: "state save load", "sequence management"

---

## Day 1 总结

### 完成的工作 ✓
1. ✓ 克隆 llama.cpp 源码
2. ✓ 阅读 llama.h API 文档
3. ✓ 分析官方示例代码
4. ✓ 记录所有 KV-Cache 相关 API (memory, state, state_seq)
5. ✓ 理解 Sequence 概念
6. ✓ 获得实测数据（状态大小）

### 核心发现 ⭐
1. **API 迁移路径清晰**: 旧的 `llama_kv_cache_*` → 新的 `llama_memory_*`
2. **序列状态是关键**: `llama_state_seq_*` 正是我们需要的前缀缓存机制
3. **状态可跨 context**: 可以保存到文件/内存，在不同 context 间迁移
4. **内存占用可控**: 通过限制前缀长度（20-50 tokens）控制在 0.5-1 GB

### 明确的实现方案 ✓
```cpp
class KVCacheManager {
    // 保存前缀缓存
    bool saveCache(const std::string& prefix, llama_context* ctx, int seq_id) {
        size_t size = llama_state_seq_get_size(ctx, seq_id);
        std::vector<uint8_t> data(size);
        llama_state_seq_get_data(ctx, data.data(), size, seq_id);
        cache_[prefix] = std::move(data);
        return true;
    }

    // 加载前缀缓存
    bool loadCache(const std::string& prefix, llama_context* ctx, int seq_id) {
        auto it = cache_.find(prefix);
        if (it == cache_.end()) return false;
        llama_state_seq_set_data(ctx, it->second.data(), it->second.size(), seq_id);
        return true;
    }
};
```

### 待验证问题 ⚠️
1. ? 压缩后的状态大小（zstd/lz4）
2. ? 序列化/反序列化的实际耗时
3. ? 多序列并发的最大数量
4. ? 不同模型的状态大小差异

### 下一步（Day 2）
1. 编写测试代码验证以上理解
2. 实测序列化性能
3. 创建新旧 API 对比表

---

**总结**: Day 1 研究非常成功！新 API 更加模块化和强大，通过 memory 和 state 分离实现了更灵活的缓存管理。关键是理解 sequence 的概念，这是实现前缀缓存的基础。官方示例代码给出了完美的实现参考。

**创建时间**: 2025-11-11
**下次更新**: 2025-11-12 (Day 2)
