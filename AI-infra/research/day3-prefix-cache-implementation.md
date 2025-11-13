# Day 3: 前缀缓存实现方案

**日期**: 2025-11-13
**目标**: 使用新 llama.cpp API 实现前缀缓存功能

---

## 现有架构分析

### 当前数据结构

**TrieNode** (`PrefixTree.h`):
```cpp
struct TrieNode {
    std::unordered_map<int, std::shared_ptr<TrieNode>> children;
    int seq_id = -1;           // llama.cpp 序列 ID
    int kv_length = 0;         // 缓存的 token 数量
    uint64_t last_used_ns = 0; // 最后使用时间
    uint32_t hit_count = 0;    // 命中次数
};
```

**问题**: 没有存储状态数据（`state_data`）

### 现有函数（需要更新）

1. `findPrefixCache()` - Line 317
2. `savePrefixCache()` - Line 362

---

## 新实现方案

### 方案 A: 在 ModelManager 中存储状态数据（推荐）

**优点**:
- PrefixTree 保持轻量级（只存储元数据）
- 状态数据集中管理，便于内存控制
- 可以使用 LRU 淘汰大数据

**数据结构**:
```cpp
// ModelManager.h 中添加
struct CacheStateData {
    std::vector<uint8_t> state_data;  // 序列化的 KV-Cache 状态
    size_t state_size;                 // 状态大小
    time_t created_at;                 // 创建时间
    time_t last_accessed;              // 最后访问时间
};

// 状态数据存储（按 seq_id 索引）
std::unordered_map<int, CacheStateData> state_cache_;
mutable std::mutex state_mutex_;
```

**工作流程**:
```
1. findPrefixCache(prefix):
   a. 使用 PrefixTree 查找最长前缀 -> 获得 seq_id 和 matched_len
   b. 从 state_cache_ 中获取 state_data
   c. 使用 llama_state_seq_set_data() 恢复到 seq_id=0
   d. 返回 matched_len

2. savePrefixCache(prefix, kv_length):
   a. 分配新的 seq_id
   b. 使用 llama_memory_seq_cp() 复制当前序列到新 seq_id
   c. 使用 llama_state_seq_get_data() 保存状态
   d. 将状态存入 state_cache_[seq_id]
   e. 插入到 PrefixTree
```

---

### 方案 B: 简化版（快速实现）

**简化策略**:
- 暂时不序列化状态数据
- 直接使用 llama.cpp 的多序列功能
- 状态保存在 llama.cpp 的 memory 中

**优点**:
- 实现简单，改动最小
- 利用 llama.cpp 现有的内存管理
- 速度快（无序列化开销）

**缺点**:
- 状态不能持久化到文件
- 重启后缓存丢失
- 内存占用可能较大

**实现**:
```cpp
// findPrefixCache() 简化版
int ModelManager::findPrefixCache(const std::string& prefix) {
    auto tokens = tokenize(prefix);
    auto [seq_id, matched_len] = prefix_tree_.findLongestPrefix(tokens);

    if (seq_id >= 0 && matched_len > 0) {
        // 直接使用 memory API 复制序列
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_rm(mem, 0, -1, -1);  // 清空序列 0
        llama_memory_seq_cp(mem, seq_id, 0, -1, -1);  // 复制到序列 0

        prefix_tree_.updateHit(tokens);
        total_cache_hits_++;
        return matched_len;
    }

    return 0;
}

// savePrefixCache() 简化版
void ModelManager::savePrefixCache(const std::string& prefix, int kv_length) {
    auto tokens = tokenize(prefix);

    // 淘汰旧缓存
    if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
        // 需要清理对应的序列
        prefix_tree_.evictLRU(MAX_PREFIX_CACHE);
    }

    int seq_id = next_seq_id_++;

    // 复制当前序列到新 seq_id
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_seq_cp(mem, 0, seq_id, 0, kv_length);

    // 插入到 Trie
    prefix_tree_.insert(tokens, seq_id);
}
```

---

## 选择的方案

**推荐**: 先实现方案 B（简化版），然后逐步升级到方案 A

**原因**:
1. 方案 B 实现快速，可以立即验证功能
2. 方案 B 的代码改动最小，风险低
3. 后续可以无缝升级到方案 A（持久化）

---

## 实现细节（方案 B）

### Step 1: 修复 savePrefixCache()

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:362`

**修改前**:
```cpp
void ModelManager::savePrefixCache(const std::string& prefix, int kv_length) {
    // TODO: Update to use new llama.cpp KV cache API
    std::cerr << "[PrefixTree] Save prefix temporarily disabled\n";
    return;
}
```

**修改后**:
```cpp
void ModelManager::savePrefixCache(const std::string& prefix, int kv_length) {
    std::lock_guard<std::mutex> g(cache_mutex_);

    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed, cannot save\n";
        return;
    }

    // LRU 淘汰
    if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
        // 找到要淘汰的 seq_id
        // TODO: 实现 getEvictedSeqIds() 获取淘汰的序列
        prefix_tree_.evictLRU(MAX_PREFIX_CACHE);
    }

    // 分配新序列 ID
    int seq_id = next_seq_id_++;

    // 使用新的 memory API 复制序列
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_seq_cp(mem, 0, seq_id, 0, kv_length);

    // 插入到 Trie
    prefix_tree_.insert(tokens, seq_id);

    std::cerr << "[PrefixTree] SAVED " << tokens.size() << " tokens"
              << " kv_len=" << kv_length
              << " seq_id=" << seq_id
              << " (total=" << prefix_tree_.size() << ")\n";
}
```

---

### Step 2: 修复 findPrefixCache()

**文件**: `AI-chats-linux/src/inference/ModelManager.cpp:317`

**修改前**:
```cpp
int ModelManager::findPrefixCache(const std::string& prefix) {
    // TODO: Update to use new llama.cpp KV cache API
    std::cerr << "[PrefixTree] Prefix caching temporarily disabled\n";
    return 0;
}
```

**修改后**:
```cpp
int ModelManager::findPrefixCache(const std::string& prefix) {
    std::lock_guard<std::mutex> g(cache_mutex_);
    total_cache_requests_++;

    auto tokens = tokenize(prefix);
    if (tokens.empty()) {
        std::cerr << "[PrefixTree] Tokenize failed\n";
        return 0;
    }

    auto [seq_id, matched_len] = prefix_tree_.findLongestPrefix(tokens);

    if (seq_id >= 0 && matched_len > 0) {
        std::cerr << "[PrefixTree] ✓ HIT! "
                  << "seq_id=" << seq_id
                  << " matched_tokens=" << matched_len << "/" << tokens.size()
                  << " (cache_size=" << prefix_tree_.size() << ")\n";

        // 使用新的 memory API 复制序列
        llama_memory_t mem = llama_get_memory(ctx_);
        llama_memory_seq_rm(mem, 0, -1, -1);  // 清空序列 0
        llama_memory_seq_cp(mem, seq_id, 0, 0, matched_len);  // 复制到序列 0

        prefix_tree_.updateHit(tokens);
        total_cache_hits_++;

        double hit_rate = (double)total_cache_hits_ / total_cache_requests_ * 100.0;
        std::cerr << "[PrefixTree] Current hit rate: " << hit_rate << "% ("
                  << total_cache_hits_ << "/" << total_cache_requests_ << ")\n";

        return matched_len;
    }

    std::cerr << "[PrefixTree] ✗ MISS for " << tokens.size() << " tokens"
              << " (cache_size=" << prefix_tree_.size() << ")\n";
    return 0;
}
```

---

### Step 3: 清理序列的问题

**问题**: 当 PrefixTree 淘汰缓存时，需要同步清理 llama.cpp 中的对应序列

**解决**: 修改 PrefixTree::evictLRU() 返回被淘汰的 seq_id

**PrefixTree.h** 修改:
```cpp
// 返回被淘汰的 seq_id 列表
std::vector<int> evictLRU(size_t max_entries);
```

**ModelManager.cpp** 中使用:
```cpp
if (prefix_tree_.size() >= MAX_PREFIX_CACHE) {
    auto evicted_seq_ids = prefix_tree_.evictLRU(MAX_PREFIX_CACHE);

    // 清理对应的序列
    llama_memory_t mem = llama_get_memory(ctx_);
    for (int sid : evicted_seq_ids) {
        llama_memory_seq_rm(mem, sid, -1, -1);
    }
}
```

---

## 测试计划

### 单元测试

1. **测试 savePrefixCache()**
   ```cpp
   // 保存前缀
   manager->run("Hello world", "test1", 64);
   // 检查 prefix_tree_.size() == 1
   ```

2. **测试 findPrefixCache()**
   ```cpp
   // 第一次请求
   manager->run("Hello world", "test1", 64);
   // 第二次请求（应该命中）
   manager->run("Hello world!", "test2", 64);
   // 检查缓存命中率
   ```

3. **测试 LRU 淘汰**
   ```cpp
   // 填满缓存
   for (int i = 0; i < MAX_PREFIX_CACHE + 1; i++) {
       manager->run("Prompt " + std::to_string(i), "test", 64);
   }
   // 检查 prefix_tree_.size() <= MAX_PREFIX_CACHE
   ```

### 集成测试

```bash
# 测试 1: 相同 prompt 两次请求
curl -X POST http://localhost:8081/infer \
  -d '{"prompt":"Hello, how are you?","chat_id":"test1"}'

curl -X POST http://localhost:8081/infer \
  -d '{"prompt":"Hello, how are you doing?","chat_id":"test2"}'
# 应该看到 "[PrefixTree] ✓ HIT!" 日志

# 测试 2: 多轮对话
curl -X POST http://localhost:8081/infer \
  -d '{"prompt":"What is Python?","chat_id":"test3"}'

curl -X POST http://localhost:8081/infer \
  -d '{"prompt":"What is Python used for?","chat_id":"test4"}'
# 应该看到缓存命中
```

---

## 性能预期

基于 Day 1-2 的研究数据：

**状态大小**:
- 20 tokens 前缀 ≈ 400 MB
- 建议缓存前缀长度: 10-30 tokens

**性能提升**:
- 缓存命中时延迟减少: 30-50%
- 预期缓存命中率: 40-60% (取决于场景)

**内存占用**:
- 10 个缓存条目 × 20 tokens ≈ 4 GB
- 需要控制缓存条目数量

---

## 潜在问题与解决

### 问题 1: 内存占用过大

**解决**:
- 限制 MAX_PREFIX_CACHE = 10-20
- 限制前缀长度 < 30 tokens
- 实现更激进的淘汰策略

### 问题 2: 序列 ID 耗尽

**问题**: `next_seq_id_` 持续增长

**解决**:
```cpp
// 回收被淘汰的 seq_id
std::set<int> available_seq_ids_;

int allocateSeqId() {
    if (!available_seq_ids_.empty()) {
        int sid = *available_seq_ids_.begin();
        available_seq_ids_.erase(available_seq_ids_.begin());
        return sid;
    }
    return next_seq_id_++;
}

void releaseSeqId(int seq_id) {
    available_seq_ids_.insert(seq_id);
}
```

### 问题 3: 多线程并发

**当前状态**: 使用 `cache_mutex_` 保护

**验证**: 压力测试多并发请求

---

## 后续优化（方案 A）

### 状态序列化持久化

```cpp
// 保存到文件
void ModelManager::savePrefixCacheToFile(const std::string& cache_dir) {
    for (auto& [seq_id, data] : state_cache_) {
        std::string filename = cache_dir + "/cache_" + std::to_string(seq_id) + ".bin";
        std::ofstream file(filename, std::ios::binary);
        file.write((char*)data.state_data.data(), data.state_size);
    }
}

// 从文件加载
void ModelManager::loadPrefixCacheFromFile(const std::string& cache_dir) {
    // 扫描目录，加载所有 .bin 文件
    // 恢复到对应的 seq_id
}
```

### 状态压缩

```cpp
#include <zstd.h>

std::vector<uint8_t> compressState(const std::vector<uint8_t>& data) {
    size_t compressed_bound = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed(compressed_bound);

    size_t compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        3  // compression level
    );

    compressed.resize(compressed_size);
    return compressed;
}
```

---

## 总结

**Day 3 目标**:
1. ✅ 分析现有架构
2. ✅ 设计实现方案
3. ⏳ 实现 findPrefixCache()
4. ⏳ 实现 savePrefixCache()
5. ⏳ 测试验证

**明确的实现路径**:
- 方案 B（简化版）→ 快速验证功能
- 方案 A（完整版）→ 后续优化升级

---

**创建时间**: 2025-11-13
**版本**: v1.0
