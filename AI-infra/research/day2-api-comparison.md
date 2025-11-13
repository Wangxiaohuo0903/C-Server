# Day 2: llama.cpp API 对比分析

**日期**: 2025-11-12
**目标**: 对比新旧 API，明确迁移路径

---

## API 变更总览

### 重大变化

1. **弃用 `llama_kv_cache_*` 系列函数**
   - 原因：API 设计重构，使用更通用的 `llama_memory_*`
   - 影响：所有 KV-Cache 直接操作需要改用 memory API

2. **引入 `llama_memory_t` 抽象**
   - 统一管理内存和 KV-Cache
   - 支持不同的内存后端（Transformer, Mamba 等）

3. **新增 `llama_state_seq_*` 序列级状态管理**
   - 更细粒度的状态控制
   - 支持单序列保存/恢复

---

## 详细 API 对比表

### 1. 模型和上下文管理

| 功能 | 旧 API | 新 API | 变化说明 |
|------|--------|--------|----------|
| 加载模型 | `llama_load_model_from_file()` | `llama_model_load()` | 函数重命名 |
| 释放模型 | `llama_free_model()` | `llama_model_free()` | 函数重命名 |
| 创建上下文 | `llama_new_context_with_model()` | `llama_init_from_model()` | 函数重命名，参数相同 |
| 释放上下文 | `llama_free()` | `llama_free()` | 无变化 |

**迁移示例**:
```cpp
// 旧代码
llama_model* model = llama_load_model_from_file(path, params);
llama_context* ctx = llama_new_context_with_model(model, ctx_params);
// ...
llama_free_model(model);

// 新代码
llama_model* model = llama_model_load(path, params);
llama_context* ctx = llama_init_from_model(model, ctx_params);
// ...
llama_model_free(model);
```

---

### 2. KV-Cache 管理（核心变更）

#### 2.1 清空 KV-Cache

| 旧 API | 新 API | 迁移难度 |
|--------|--------|----------|
| `llama_kv_cache_clear(ctx)` | `llama_memory_clear(llama_get_memory(ctx), true)` | ⭐ 简单 |

**对比**:
```cpp
// 旧代码
llama_kv_cache_clear(ctx);

// 新代码
llama_memory_t mem = llama_get_memory(ctx);
llama_memory_clear(mem, true);  // data=true 清空数据和元数据
```

**参数说明**:
- `data=true`: 清空数据和元数据
- `data=false`: 仅清空元数据（保留数据）

---

#### 2.2 删除指定序列

| 旧 API | 新 API | 迁移难度 |
|--------|--------|----------|
| `llama_kv_cache_seq_rm(ctx, seq_id, p0, p1)` | `llama_memory_seq_rm(llama_get_memory(ctx), seq_id, p0, p1)` | ⭐ 简单 |

**对比**:
```cpp
// 旧代码
llama_kv_cache_seq_rm(ctx, seq_id, 0, -1);  // 删除整个序列

// 新代码
llama_memory_t mem = llama_get_memory(ctx);
llama_memory_seq_rm(mem, seq_id, 0, -1);
```

**位置参数说明**:
- `p0 >= 0, p1 >= 0`: 删除 `[p0, p1)` 范围
- `p0 < 0`: 从头开始 `[0, p1)`
- `p1 < 0`: 到末尾 `[p0, inf)`
- `p0 < 0, p1 < 0`: 删除整个序列

---

#### 2.3 复制序列

| 旧 API | 新 API | 迁移难度 |
|--------|--------|----------|
| `llama_kv_cache_seq_cp(ctx, src, dst, p0, p1)` | `llama_memory_seq_cp(llama_get_memory(ctx), src, dst, p0, p1)` | ⭐ 简单 |

**对比**:
```cpp
// 旧代码
llama_kv_cache_seq_cp(ctx, 0, 1, -1, -1);  // 复制序列 0 到序列 1

// 新代码
llama_memory_t mem = llama_get_memory(ctx);
llama_memory_seq_cp(mem, 0, 1, -1, -1);
```

**用途**: 分支对话
```cpp
// 场景: 用户想尝试不同的回复方向
// 1. 保留原对话（seq_id=0）
// 2. 复制到新序列（seq_id=1）
// 3. 在 seq_id=1 上继续推理
llama_memory_seq_cp(mem, 0, 1, -1, -1);
```

---

#### 2.4 保留指定序列

| 旧 API | 新 API | 迁移难度 |
|--------|--------|----------|
| `llama_kv_cache_seq_keep(ctx, seq_id)` | `llama_memory_seq_keep(llama_get_memory(ctx), seq_id)` | ⭐ 简单 |

**对比**:
```cpp
// 旧代码
llama_kv_cache_seq_keep(ctx, 0);  // 删除所有非 0 序列

// 新代码
llama_memory_t mem = llama_get_memory(ctx);
llama_memory_seq_keep(mem, 0);
```

**用途**: 清理其他序列，只保留当前序列

---

#### 2.5 调整序列位置（新功能）

| 旧 API | 新 API | 说明 |
|--------|--------|------|
| ❌ 不存在 | `llama_memory_seq_add()` | 添加位置偏移 |
| ❌ 不存在 | `llama_memory_seq_div()` | 位置除法 |

**新功能**:
```cpp
// 给序列所有位置加上偏移（用于 sliding window）
llama_memory_seq_add(mem, seq_id, 0, -1, delta);

// 位置缩放（用于压缩位置空间）
llama_memory_seq_div(mem, seq_id, 0, -1, 2);  // 位置除以 2
```

---

### 3. 状态序列化（全新功能）

旧版没有状态序列化 API，这是新版的核心创新。

#### 3.1 全局状态保存/恢复

| 功能 | 新 API | 说明 |
|------|--------|------|
| 获取状态大小 | `llama_state_get_size(ctx)` | 返回字节数 |
| 保存状态到内存 | `llama_state_get_data(ctx, dst, size)` | 序列化到缓冲区 |
| 从内存恢复状态 | `llama_state_set_data(ctx, src, size)` | 反序列化 |
| 保存状态到文件 | `llama_state_save_file(ctx, path, tokens, n)` | 持久化 |
| 从文件恢复状态 | `llama_state_load_file(ctx, path, tokens, cap, n)` | 加载 |

**使用示例**:
```cpp
// 保存状态
size_t size = llama_state_get_size(ctx);
std::vector<uint8_t> state_data(size);
llama_state_get_data(ctx, state_data.data(), size);

// 恢复状态（可以是不同的 context）
llama_context* ctx2 = llama_init_from_model(model, params);
llama_state_set_data(ctx2, state_data.data(), size);
```

---

#### 3.2 单序列状态保存/恢复（重要！）

| 功能 | 新 API | 说明 |
|------|--------|------|
| 获取序列状态大小 | `llama_state_seq_get_size(ctx, seq_id)` | 比全局状态更小 |
| 保存序列状态 | `llama_state_seq_get_data(ctx, dst, size, seq_id)` | 序列化单个序列 |
| 恢复序列状态 | `llama_state_seq_set_data(ctx, src, size, dest_seq_id)` | 可恢复到不同 seq_id |
| 保存序列到文件 | `llama_state_seq_save_file(ctx, path, seq_id, tokens, n)` | 持久化单序列 |
| 从文件恢复序列 | `llama_state_seq_load_file(ctx, path, dest_seq_id, ...)` | 加载单序列 |

**前缀缓存应用**:
```cpp
// === 场景: 缓存前缀 "给这段代码添加注释: " ===

// 1. 推理前缀
std::vector<llama_token> prefix_tokens = tokenize("给这段代码添加注释: ");
llama_decode(ctx, make_batch(prefix_tokens, 0));  // seq_id=0

// 2. 保存前缀状态
size_t prefix_size = llama_state_seq_get_size(ctx, 0);
std::vector<uint8_t> prefix_cache(prefix_size);
llama_state_seq_get_data(ctx, prefix_cache.data(), prefix_size, 0);

// 3. 下次请求: 恢复前缀缓存
llama_state_seq_set_data(ctx, prefix_cache.data(), prefix_cache.size(), 1);  // seq_id=1
// 4. 只需推理后续内容
llama_decode(ctx, make_batch(code_tokens, 1));  // seq_id=1，从前缀末尾继续
```

---

### 4. 序列查询（新功能）

| 功能 | 新 API | 说明 |
|------|--------|------|
| 查询序列最小位置 | `llama_memory_seq_pos_min(mem, seq_id)` | 空序列返回 -1 |
| 查询序列最大位置 | `llama_memory_seq_pos_max(mem, seq_id)` | 空序列返回 -1 |

**使用场景**:
```cpp
llama_memory_t mem = llama_get_memory(ctx);
llama_pos min_pos = llama_memory_seq_pos_min(mem, seq_id);
llama_pos max_pos = llama_memory_seq_pos_max(mem, seq_id);

if (min_pos == -1) {
    printf("序列 %d 是空的\n", seq_id);
} else {
    printf("序列 %d 范围: [%d, %d]，长度 %d\n",
           seq_id, min_pos, max_pos, max_pos - min_pos + 1);
}
```

---

### 5. 我们项目中需要修改的代码

#### 5.1 ModelManager::resetContext()

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:71`

**当前代码** (已临时注释):
```cpp
void ModelManager::resetContext(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (!ctx_) return;
    // llama_kv_cache_clear(ctx_);  // TODO: API changed
}
```

**修改方案**:
```cpp
void ModelManager::resetContext(const std::string& chat_id) {
    std::lock_guard<std::mutex> g(mutex_);
    if (!ctx_) return;

    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_clear(mem, true);  // 清空所有数据
}
```

---

#### 5.2 ModelManager::warmup()

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:125-145`

**当前代码** (已临时注释):
```cpp
void ModelManager::warmup() {
    // TODO: Warmup logic disabled due to API changes
    std::cerr << "[Warmup] Warmup temporarily disabled (API migration needed)\n";
}
```

**修改方案 1: 使用序列状态**:
```cpp
void ModelManager::warmup() {
    if (warmup_templates_.empty()) return;

    for (const auto& tmpl : warmup_templates_) {
        // 1. Tokenize 前缀
        std::vector<llama_token> tokens = tokenizePrompt(tmpl.prefix);

        // 2. 推理前缀（使用临时序列 ID）
        int temp_seq_id = 999;
        // ... 调用 llama_decode ...

        // 3. 保存前缀状态
        size_t state_size = llama_state_seq_get_size(ctx_, temp_seq_id);
        std::vector<uint8_t> state_data(state_size);
        llama_state_seq_get_data(ctx_, state_data.data(), state_size, temp_seq_id);

        // 4. 存入缓存
        cache_manager_->saveCache(tmpl.cache_key, state_data);

        // 5. 清理临时序列
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_rm(mem, temp_seq_id, -1, -1);
    }
}
```

**修改方案 2: 使用文件缓存**:
```cpp
void ModelManager::warmup() {
    for (const auto& tmpl : warmup_templates_) {
        std::string cache_file = "cache/" + tmpl.cache_key + ".bin";

        // 检查缓存是否已存在
        if (std::filesystem::exists(cache_file)) {
            std::cerr << "[Warmup] Cache exists: " << tmpl.cache_key << "\n";
            continue;
        }

        // ... tokenize and decode ...

        // 保存到文件
        llama_state_seq_save_file(
            ctx_, cache_file.c_str(), seq_id,
            tokens.data(), tokens.size()
        );
    }
}
```

---

#### 5.3 前缀缓存函数（需完全重写）

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:315-424`

**当前状态**: 全部注释为 TODO

**新实现方案**: 见下一节完整实现

---

## 完整实现示例

### 新的 KVCacheManager 类

```cpp
class KVCacheManager {
public:
    struct Config {
        size_t max_entries = 100;
        size_t max_memory_mb = 1024;
        int min_prefix_tokens = 10;
        int ttl_seconds = 3600;
    };

    struct CacheEntry {
        std::string cache_key;
        std::vector<uint8_t> state_data;
        std::vector<llama_token> prefix_tokens;
        size_t state_size;
        time_t created_at;
        time_t last_accessed;
        uint64_t hit_count;
    };

    KVCacheManager(const Config& cfg) : config_(cfg) {}

    // 保存前缀缓存
    bool saveCache(
        const std::string& cache_key,
        llama_context* ctx,
        llama_seq_id seq_id,
        const std::vector<llama_token>& prefix_tokens
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        // 1. 获取状态大小
        size_t state_size = llama_state_seq_get_size(ctx, seq_id);
        if (state_size == 0) {
            std::cerr << "[Cache] Failed to get state size\n";
            return false;
        }

        // 2. 检查内存限制
        if (!checkMemoryLimit(state_size)) {
            evictLRU();  // 淘汰最久未使用
        }

        // 3. 分配缓冲区
        std::vector<uint8_t> state_data(state_size);

        // 4. 保存状态
        size_t copied = llama_state_seq_get_data(
            ctx, state_data.data(), state_size, seq_id
        );

        if (copied != state_size) {
            std::cerr << "[Cache] State copy incomplete\n";
            return false;
        }

        // 5. 存入缓存
        CacheEntry entry{
            .cache_key = cache_key,
            .state_data = std::move(state_data),
            .prefix_tokens = prefix_tokens,
            .state_size = copied,
            .created_at = time(nullptr),
            .last_accessed = time(nullptr),
            .hit_count = 0
        };

        cache_[cache_key] = std::move(entry);
        total_memory_bytes_ += copied;

        std::cerr << "[Cache] Saved: " << cache_key
                  << " (" << copied << " bytes)\n";
        return true;
    }

    // 加载前缀缓存
    std::shared_ptr<CacheEntry> loadCache(
        const std::string& cache_key,
        llama_context* ctx,
        llama_seq_id dest_seq_id
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = cache_.find(cache_key);
        if (it == cache_.end()) {
            return nullptr;  // 未命中
        }

        // 恢复状态
        size_t loaded = llama_state_seq_set_data(
            ctx,
            it->second.state_data.data(),
            it->second.state_size,
            dest_seq_id
        );

        if (loaded == 0) {
            std::cerr << "[Cache] Failed to load state\n";
            return nullptr;
        }

        // 更新统计
        it->second.last_accessed = time(nullptr);
        it->second.hit_count++;
        total_hits_++;

        std::cerr << "[Cache] Hit: " << cache_key
                  << " (" << loaded << " bytes)\n";

        return std::make_shared<CacheEntry>(it->second);
    }

    // 查找最长前缀
    std::string findLongestPrefix(const std::vector<llama_token>& tokens) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string best_key;
        size_t best_len = 0;

        for (const auto& [key, entry] : cache_) {
            size_t match_len = countMatchingTokens(tokens, entry.prefix_tokens);

            if (match_len >= config_.min_prefix_tokens && match_len > best_len) {
                best_key = key;
                best_len = match_len;
            }
        }

        if (!best_key.empty()) {
            total_requests_++;
        }

        return best_key;
    }

private:
    bool checkMemoryLimit(size_t new_size) {
        size_t limit = config_.max_memory_mb * 1024 * 1024;
        return (total_memory_bytes_ + new_size) <= limit;
    }

    void evictLRU() {
        if (cache_.empty()) return;

        auto oldest = std::min_element(
            cache_.begin(), cache_.end(),
            [](const auto& a, const auto& b) {
                return a.second.last_accessed < b.second.last_accessed;
            }
        );

        total_memory_bytes_ -= oldest->second.state_size;
        cache_.erase(oldest);
    }

    size_t countMatchingTokens(
        const std::vector<llama_token>& a,
        const std::vector<llama_token>& b
    ) {
        size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            if (a[i] != b[i]) return i;
        }
        return n;
    }

    Config config_;
    std::map<std::string, CacheEntry> cache_;
    std::mutex mutex_;
    size_t total_memory_bytes_ = 0;
    uint64_t total_requests_ = 0;
    uint64_t total_hits_ = 0;
};
```

---

## 迁移检查清单

### 阶段 1: 基础 API 替换 ✓

- [x] `llama_free_model` → `llama_model_free`
- [x] `llama_load_model_from_file` → `llama_model_load`
- [x] `llama_new_context_with_model` → `llama_init_from_model`

### 阶段 2: Memory API 迁移（本周）

- [ ] `llama_kv_cache_clear` → `llama_memory_clear`
- [ ] `llama_kv_cache_seq_rm` → `llama_memory_seq_rm`
- [ ] `llama_kv_cache_seq_cp` → `llama_memory_seq_cp`
- [ ] `llama_kv_cache_seq_keep` → `llama_memory_seq_keep`

### 阶段 3: 状态序列化实现（下周）

- [ ] 实现 `KVCacheManager::saveCache()` 使用 `llama_state_seq_get_data`
- [ ] 实现 `KVCacheManager::loadCache()` 使用 `llama_state_seq_set_data`
- [ ] 实现 `findLongestPrefix()` 前缀匹配
- [ ] 实现 LRU 淘汰策略
- [ ] 实现缓存统计

### 阶段 4: 测试验证

- [ ] 单元测试: 状态保存/恢复
- [ ] 集成测试: 前缀缓存命中率
- [ ] 性能测试: 序列化耗时
- [ ] 压力测试: 多序列并发

---

## 关键差异总结

### 优势

1. **更灵活**: `llama_memory_*` 支持不同内存后端
2. **更细粒度**: `llama_state_seq_*` 支持单序列操作
3. **持久化支持**: 状态可保存到文件
4. **跨 context**: 状态可在不同 context 间迁移

### 挑战

1. **API 变复杂**: 需要多一步 `llama_get_memory()`
2. **内存开销**: 状态序列化占用较大内存（~20MB/token）
3. **迁移成本**: 需要重写所有缓存逻辑

### 建议

1. **优先使用序列状态**: `llama_state_seq_*` 比全局状态更轻量
2. **合理控制前缀长度**: 建议 20-50 tokens
3. **考虑压缩**: 对状态数据使用 zstd 压缩
4. **缓存失效策略**: 结合 LRU 和 TTL

---

## 下一步行动

### Day 2 剩余任务
1. ✓ 创建 API 对比表（本文档）
2. ⏳ 编写测试代码验证理解
3. ⏳ 测量序列化性能

### Day 3 任务
1. 深入测试 Sequence 分支和合并
2. 多序列并发场景测试
3. 设计序列分配策略

---

**创建时间**: 2025-11-12
**下次更新**: 2025-11-13 (Day 3)
