# KVCacheManager v2 架构设计

**版本**: v2.0
**设计日期**: 2025-11-10
**目标**: 基于 llama.cpp 新 API 重新设计 KV-Cache 前缀缓存系统

---

## 📋 设计目标

### 核心目标
1. **高缓存命中率**: 代码修改场景 > 60%，多轮对话 > 40%
2. **显著性能提升**: 命中时延迟减少 > 30%
3. **内存可控**: 缓存总大小 < 2GB，可配置
4. **易于扩展**: 支持多种缓存策略，便于实验对比

### 非功能需求
- 线程安全（支持并发请求）
- 低开销（缓存管理不应成为瓶颈）
- 可观测性（提供详细的统计信息）
- 可配置性（支持参数调优）

---

## 🏗️ 整体架构

### 系统分层

```
┌─────────────────────────────────────────────────────────┐
│                   HTTP Server Layer                     │
│              (HttpServer, Router, Handler)              │
└──────────────────────┬──────────────────────────────────┘
                       │ /infer request
                       ↓
┌─────────────────────────────────────────────────────────┐
│                 Inference Manager Layer                 │
│                    (ModelManager)                       │
│  ┌─────────────────────────────────────────────────┐   │
│  │  1. 接收用户请求 (prompt, chat_id)              │   │
│  │  2. 调用 KVCacheManager 查找前缀               │   │
│  │  3. 命中 → 加载缓存，继续推理                  │   │
│  │  4. 未命中 → 从头推理，保存缓存                │   │
│  │  5. 返回推理结果                                │   │
│  └─────────────────────────────────────────────────┘   │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ↓
┌─────────────────────────────────────────────────────────┐
│              KV-Cache Management Layer                  │
│                  (KVCacheManager v2)                    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Prefix Index │  │ Cache Store  │  │ Eviction Mgr │  │
│  │  (前缀索引)  │  │  (缓存存储)  │  │  (淘汰管理)  │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ↓
┌─────────────────────────────────────────────────────────┐
│                 llama.cpp API Layer                     │
│  llama_state_get_data() / llama_state_set_data()       │
│  llama_kv_cache_seq_*() ...                            │
└─────────────────────────────────────────────────────────┘
```

---

## 📊 核心数据结构

### 1. 缓存条目 (CacheEntry)

```cpp
/**
 * @brief 单个缓存条目
 *
 * 存储一个完整的 KV-Cache 状态及其元数据
 */
struct CacheEntry {
    // ========== 标识信息 ==========
    std::string cache_key;        // 缓存键（通常是前缀内容的哈希）
    std::string prefix_content;   // 前缀的原始文本
    std::vector<int> prefix_tokens; // 前缀的 token 序列

    // ========== 缓存数据 ==========
    std::vector<uint8_t> state_data;  // llama.cpp 状态数据
    size_t state_size;                 // 状态大小（字节）
    int token_count;                   // 缓存的 token 数量

    // ========== 元数据 ==========
    time_t created_at;      // 创建时间
    time_t last_accessed;   // 最后访问时间
    uint64_t access_count;  // 访问次数

    // ========== 统计信息 ==========
    uint64_t hit_count;     // 命中次数
    double avg_save_time;   // 平均保存时间（毫秒）
    double avg_load_time;   // 平均加载时间（毫秒）

    // ========== 辅助方法 ==========
    size_t getMemoryUsage() const {
        return state_data.size() +
               prefix_content.size() +
               prefix_tokens.size() * sizeof(int);
    }

    bool isExpired(int ttl_seconds) const {
        return (time(nullptr) - last_accessed) > ttl_seconds;
    }
};
```

### 2. 前缀树节点 (TrieNode)

```cpp
/**
 * @brief 前缀树节点
 *
 * 用于快速查找最长公共前缀
 */
struct TrieNode {
    int token;                              // 当前 token
    std::shared_ptr<CacheEntry> cache;      // 关联的缓存条目（叶子节点）
    std::map<int, std::shared_ptr<TrieNode>> children;  // 子节点

    bool isLeaf() const {
        return cache != nullptr;
    }

    // 查找子节点
    std::shared_ptr<TrieNode> getChild(int token) {
        auto it = children.find(token);
        return (it != children.end()) ? it->second : nullptr;
    }
};
```

### 3. 缓存统计 (CacheStats)

```cpp
/**
 * @brief 缓存统计信息
 */
struct CacheStats {
    // ========== 请求统计 ==========
    uint64_t total_requests = 0;      // 总请求数
    uint64_t cache_hits = 0;          // 缓存命中数
    uint64_t cache_misses = 0;        // 缓存未命中数
    uint64_t partial_hits = 0;        // 部分命中（前缀匹配但不完全）

    // ========== 性能统计 ==========
    double total_save_time_ms = 0.0;  // 总保存时间
    double total_load_time_ms = 0.0;  // 总加载时间
    double total_search_time_ms = 0.0; // 总查找时间

    // ========== 缓存状态 ==========
    size_t current_entries = 0;       // 当前缓存条目数
    size_t total_memory_bytes = 0;    // 总内存使用
    size_t max_entry_size = 0;        // 最大条目大小
    size_t min_entry_size = SIZE_MAX; // 最小条目大小

    // ========== 淘汰统计 ==========
    uint64_t evictions = 0;           // 淘汰次数
    uint64_t evictions_lru = 0;       // LRU 淘汰次数
    uint64_t evictions_ttl = 0;       // TTL 过期淘汰次数
    uint64_t evictions_size = 0;      // 大小限制淘汰次数

    // ========== 计算方法 ==========
    double getCacheHitRate() const {
        return total_requests > 0 ?
            (double)cache_hits / total_requests : 0.0;
    }

    double getAvgSaveTime() const {
        return cache_hits > 0 ?
            total_save_time_ms / cache_hits : 0.0;
    }

    double getAvgLoadTime() const {
        return cache_hits > 0 ?
            total_load_time_ms / cache_hits : 0.0;
    }

    double getAvgMemoryPerEntry() const {
        return current_entries > 0 ?
            (double)total_memory_bytes / current_entries : 0.0;
    }
};
```

---

## 🔧 核心类设计

### KVCacheManager

```cpp
/**
 * @brief KV-Cache 管理器 v2
 *
 * 负责 KV-Cache 的保存、加载、查找和淘汰
 *
 * 线程安全，支持并发访问
 */
class KVCacheManager {
public:
    // ========== 构造与配置 ==========

    /**
     * @brief 配置参数
     */
    struct Config {
        // 缓存容量
        size_t max_entries = 100;           // 最大缓存条目数
        size_t max_memory_mb = 1024;        // 最大内存使用（MB）

        // 前缀匹配
        int min_prefix_tokens = 10;         // 最小前缀长度
        float prefix_match_threshold = 0.8; // 前缀匹配阈值

        // 淘汰策略
        int ttl_seconds = 3600;             // 缓存过期时间（秒）
        enum EvictionPolicy {
            LRU,        // Least Recently Used
            LFU,        // Least Frequently Used
            TTL,        // Time To Live
            MIXED       // 混合策略
        } eviction_policy = LRU;

        // 性能优化
        bool enable_compression = false;    // 是否压缩缓存数据
        int prefetch_threads = 2;           // 预加载线程数

        // 调试
        bool verbose_logging = false;       // 详细日志
    };

    explicit KVCacheManager(const Config& config = Config());
    ~KVCacheManager();

    // ========== 核心接口 ==========

    /**
     * @brief 查找最长匹配前缀
     *
     * @param tokens 输入的 token 序列
     * @return 匹配的缓存条目指针，未找到返回 nullptr
     */
    std::shared_ptr<CacheEntry> findLongestPrefix(
        const std::vector<int>& tokens
    );

    /**
     * @brief 保存 KV-Cache 状态
     *
     * @param prefix_tokens 前缀 token 序列
     * @param prefix_content 前缀原始文本
     * @param ctx llama.cpp 上下文
     * @param seq_id sequence ID
     * @return 是否保存成功
     */
    bool saveCache(
        const std::vector<int>& prefix_tokens,
        const std::string& prefix_content,
        llama_context* ctx,
        int seq_id = 0
    );

    /**
     * @brief 加载 KV-Cache 状态
     *
     * @param entry 缓存条目
     * @param ctx llama.cpp 上下文
     * @param seq_id sequence ID
     * @return 是否加载成功
     */
    bool loadCache(
        std::shared_ptr<CacheEntry> entry,
        llama_context* ctx,
        int seq_id = 0
    );

    /**
     * @brief 手动清除指定缓存
     *
     * @param cache_key 缓存键
     * @return 是否删除成功
     */
    bool removeCache(const std::string& cache_key);

    /**
     * @brief 清除所有缓存
     */
    void clearAll();

    // ========== 统计与监控 ==========

    /**
     * @brief 获取统计信息
     */
    CacheStats getStats() const;

    /**
     * @brief 打印统计报告
     */
    void printStats() const;

    /**
     * @brief 重置统计信息
     */
    void resetStats();

    // ========== 配置管理 ==========

    /**
     * @brief 更新配置
     */
    void updateConfig(const Config& new_config);

    /**
     * @brief 获取当前配置
     */
    Config getConfig() const;

private:
    // ========== 内部方法 ==========

    /**
     * @brief 前缀树查找
     */
    std::shared_ptr<TrieNode> searchTrie(
        const std::vector<int>& tokens,
        int& matched_length
    );

    /**
     * @brief 插入到前缀树
     */
    void insertTrie(
        const std::vector<int>& tokens,
        std::shared_ptr<CacheEntry> entry
    );

    /**
     * @brief 从前缀树删除
     */
    void removeTrie(const std::vector<int>& tokens);

    /**
     * @brief 生成缓存键
     */
    std::string generateCacheKey(const std::vector<int>& tokens);

    /**
     * @brief LRU 淘汰
     */
    void evictLRU();

    /**
     * @brief LFU 淘汰
     */
    void evictLFU();

    /**
     * @brief TTL 淘汰
     */
    void evictExpired();

    /**
     * @brief 检查并执行淘汰
     */
    void checkAndEvict();

    /**
     * @brief 压缩缓存数据
     */
    std::vector<uint8_t> compressData(const std::vector<uint8_t>& data);

    /**
     * @brief 解压缩缓存数据
     */
    std::vector<uint8_t> decompressData(const std::vector<uint8_t>& data);

    /**
     * @brief 更新统计信息
     */
    void updateStats(bool hit, double time_ms);

    // ========== 成员变量 ==========

    Config config_;                                    // 配置
    std::shared_ptr<TrieNode> trie_root_;             // 前缀树根节点
    std::map<std::string, std::shared_ptr<CacheEntry>> cache_map_; // 缓存映射
    CacheStats stats_;                                 // 统计信息

    mutable std::shared_mutex mutex_;                  // 读写锁
    std::unique_ptr<std::thread> eviction_thread_;    // 后台淘汰线程
    std::atomic<bool> running_;                        // 运行标志
};
```

---

## 🔄 工作流程

### 1. 推理请求处理流程

```
用户请求: POST /infer
{
  "user_message": "给这段代码添加注释：\ndef fib(n): ...",
  "chat_id": "session-123"
}
         ↓
┌────────────────────────────────────────────────┐
│ Step 1: ModelManager 接收请求                 │
│ - 解析 prompt                                  │
│ - Tokenize: "给这段代码..." → [token1, ...]  │
└────────────────────────────────────────────────┘
         ↓
┌────────────────────────────────────────────────┐
│ Step 2: 查找前缀缓存                          │
│ entry = cacheManager.findLongestPrefix(tokens)│
└────────────────────────────────────────────────┘
         ↓
    ┌────┴────┐
    │  命中？  │
    └────┬────┘
    Yes  │  No
         │
   ┌─────┴─────┐
   ↓           ↓
┌──────┐  ┌──────────────────┐
│Step 3a│  │Step 3b: 从头推理│
│加载缓存│  │ - 初始化 context│
│      │  │ - 完整 decode   │
└──┬───┘  └────┬─────────────┘
   │           │
   │           ↓
   │      ┌──────────────────┐
   │      │Step 4b: 保存缓存│
   │      │saveCache(...)   │
   │      └────┬─────────────┘
   │           │
   └─────┬─────┘
         ↓
┌────────────────────────────────────────────────┐
│ Step 5: 继续推理剩余部分                      │
│ - decode(remaining_tokens)                     │
└────────────────────────────────────────────────┘
         ↓
┌────────────────────────────────────────────────┐
│ Step 6: 返回结果                               │
│ {                                              │
│   "answer": "...",                            │
│   "cache_hit": true/false,                    │
│   "latency_ms": 1234                          │
│ }                                              │
└────────────────────────────────────────────────┘
```

### 2. 缓存保存流程

```cpp
bool KVCacheManager::saveCache(
    const std::vector<int>& prefix_tokens,
    const std::string& prefix_content,
    llama_context* ctx,
    int seq_id
) {
    auto start = std::chrono::high_resolution_clock::now();

    // 1. 检查是否值得缓存
    if (prefix_tokens.size() < config_.min_prefix_tokens) {
        return false;  // 前缀太短，不缓存
    }

    // 2. 获取状态大小
    size_t state_size = llama_state_seq_get_size(ctx, seq_id);

    // 3. 检查内存限制
    if (stats_.total_memory_bytes + state_size >
        config_.max_memory_mb * 1024 * 1024) {
        checkAndEvict();  // 先尝试淘汰
        if (stats_.total_memory_bytes + state_size >
            config_.max_memory_mb * 1024 * 1024) {
            return false;  // 仍然超限，放弃保存
        }
    }

    // 4. 分配缓冲区
    std::vector<uint8_t> state_data(state_size);

    // 5. 保存状态
    size_t actual_size = llama_state_seq_get_data(
        ctx,
        state_data.data(),
        state_size,
        seq_id
    );
    state_data.resize(actual_size);

    // 6. 可选：压缩
    if (config_.enable_compression) {
        state_data = compressData(state_data);
    }

    // 7. 创建缓存条目
    auto entry = std::make_shared<CacheEntry>();
    entry->cache_key = generateCacheKey(prefix_tokens);
    entry->prefix_content = prefix_content;
    entry->prefix_tokens = prefix_tokens;
    entry->state_data = std::move(state_data);
    entry->state_size = actual_size;
    entry->token_count = prefix_tokens.size();
    entry->created_at = time(nullptr);
    entry->last_accessed = entry->created_at;
    entry->access_count = 0;
    entry->hit_count = 0;

    // 8. 保存到缓存
    {
        std::unique_lock lock(mutex_);
        cache_map_[entry->cache_key] = entry;
        insertTrie(prefix_tokens, entry);
        stats_.current_entries++;
        stats_.total_memory_bytes += entry->getMemoryUsage();
    }

    // 9. 统计
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(
        end - start
    ).count();
    entry->avg_save_time = elapsed;

    return true;
}
```

### 3. 缓存加载流程

```cpp
bool KVCacheManager::loadCache(
    std::shared_ptr<CacheEntry> entry,
    llama_context* ctx,
    int seq_id
) {
    auto start = std::chrono::high_resolution_clock::now();

    // 1. 解压缩（如果需要）
    std::vector<uint8_t> state_data = entry->state_data;
    if (config_.enable_compression) {
        state_data = decompressData(state_data);
    }

    // 2. 恢复状态
    size_t loaded = llama_state_seq_set_data(
        ctx,
        state_data.data(),
        state_data.size(),
        seq_id
    );

    if (loaded != entry->state_size) {
        std::cerr << "[Cache] Failed to load cache, size mismatch: "
                  << "expected " << entry->state_size
                  << ", got " << loaded << std::endl;
        return false;
    }

    // 3. 更新元数据
    {
        std::unique_lock lock(mutex_);
        entry->last_accessed = time(nullptr);
        entry->access_count++;
        entry->hit_count++;
        stats_.cache_hits++;
    }

    // 4. 统计
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(
        end - start
    ).count();
    entry->avg_load_time = (entry->avg_load_time * (entry->hit_count - 1) + elapsed)
                           / entry->hit_count;
    stats_.total_load_time_ms += elapsed;

    return true;
}
```

### 4. 前缀查找流程

```cpp
std::shared_ptr<CacheEntry> KVCacheManager::findLongestPrefix(
    const std::vector<int>& tokens
) {
    auto start = std::chrono::high_resolution_clock::now();

    std::shared_lock lock(mutex_);
    stats_.total_requests++;

    // 1. 在前缀树中查找
    int matched_length = 0;
    auto node = searchTrie(tokens, matched_length);

    // 2. 检查是否满足最小长度要求
    if (matched_length < config_.min_prefix_tokens) {
        stats_.cache_misses++;
        return nullptr;
    }

    // 3. 检查匹配率
    float match_rate = (float)matched_length / tokens.size();
    if (match_rate < config_.prefix_match_threshold) {
        stats_.partial_hits++;
        // 可以选择：部分命中是否算命中？
        // 这里我们要求至少匹配 80% 才算命中
        stats_.cache_misses++;
        return nullptr;
    }

    // 4. 返回缓存条目
    auto entry = node ? node->cache : nullptr;
    if (entry) {
        stats_.cache_hits++;
    } else {
        stats_.cache_misses++;
    }

    // 5. 统计
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(
        end - start
    ).count();
    stats_.total_search_time_ms += elapsed;

    return entry;
}
```

---

## 🎯 优化策略

### 1. LRU 淘汰策略

```cpp
void KVCacheManager::evictLRU() {
    if (cache_map_.empty()) return;

    // 找到最久未使用的条目
    auto oldest = std::min_element(
        cache_map_.begin(),
        cache_map_.end(),
        [](const auto& a, const auto& b) {
            return a.second->last_accessed < b.second->last_accessed;
        }
    );

    // 删除
    auto entry = oldest->second;
    stats_.total_memory_bytes -= entry->getMemoryUsage();
    stats_.current_entries--;
    stats_.evictions++;
    stats_.evictions_lru++;

    removeTrie(entry->prefix_tokens);
    cache_map_.erase(oldest);

    std::cerr << "[Cache] Evicted LRU entry: " << entry->cache_key
              << " (last accessed: " << entry->last_accessed << ")"
              << std::endl;
}
```

### 2. TTL 过期淘汰

```cpp
void KVCacheManager::evictExpired() {
    std::unique_lock lock(mutex_);

    auto it = cache_map_.begin();
    while (it != cache_map_.end()) {
        if (it->second->isExpired(config_.ttl_seconds)) {
            // 过期，删除
            stats_.total_memory_bytes -= it->second->getMemoryUsage();
            stats_.current_entries--;
            stats_.evictions++;
            stats_.evictions_ttl++;

            removeTrie(it->second->prefix_tokens);
            it = cache_map_.erase(it);
        } else {
            ++it;
        }
    }
}
```

### 3. 智能预测性缓存

```cpp
/**
 * @brief 预测性缓存
 *
 * 根据历史模式，预测可能需要的缓存并预加载
 */
class PredictiveCache {
public:
    /**
     * @brief 记录访问模式
     */
    void recordAccess(const std::vector<int>& tokens) {
        // 提取前缀模式
        std::string pattern = extractPattern(tokens);

        // 更新频率统计
        pattern_freq_[pattern]++;

        // 如果某个模式频率很高，预加载相关缓存
        if (pattern_freq_[pattern] > THRESHOLD) {
            prefetchRelatedCache(pattern);
        }
    }

private:
    std::map<std::string, int> pattern_freq_;
    const int THRESHOLD = 5;
};
```

---

## 📈 性能指标监控

### Prometheus 格式导出

```cpp
std::string KVCacheManager::exportPrometheusMetrics() const {
    std::ostringstream oss;
    auto stats = getStats();

    // 命中率
    oss << "# HELP cache_hit_rate Cache hit rate\n";
    oss << "# TYPE cache_hit_rate gauge\n";
    oss << "cache_hit_rate " << stats.getCacheHitRate() << "\n";

    // 请求数
    oss << "# HELP cache_requests_total Total cache requests\n";
    oss << "# TYPE cache_requests_total counter\n";
    oss << "cache_requests_total " << stats.total_requests << "\n";

    // 命中数
    oss << "# HELP cache_hits_total Total cache hits\n";
    oss << "# TYPE cache_hits_total counter\n";
    oss << "cache_hits_total " << stats.cache_hits << "\n";

    // 内存使用
    oss << "# HELP cache_memory_bytes Cache memory usage in bytes\n";
    oss << "# TYPE cache_memory_bytes gauge\n";
    oss << "cache_memory_bytes " << stats.total_memory_bytes << "\n";

    // 条目数
    oss << "# HELP cache_entries Current cache entries\n";
    oss << "# TYPE cache_entries gauge\n";
    oss << "cache_entries " << stats.current_entries << "\n";

    // 平均加载时间
    oss << "# HELP cache_load_time_ms Average cache load time in ms\n";
    oss << "# TYPE cache_load_time_ms gauge\n";
    oss << "cache_load_time_ms " << stats.getAvgLoadTime() << "\n";

    return oss.str();
}
```

---

## 🧪 测试计划

### 1. 单元测试

```cpp
// tests/kv_cache_manager_test.cpp

TEST(KVCacheManagerTest, SaveAndLoad) {
    KVCacheManager manager;
    llama_context* ctx = /* ... */;

    std::vector<int> tokens = {1, 2, 3, 4, 5};
    std::string content = "Hello world";

    // 保存
    ASSERT_TRUE(manager.saveCache(tokens, content, ctx, 0));

    // 查找
    auto entry = manager.findLongestPrefix(tokens);
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->token_count, 5);

    // 加载
    ASSERT_TRUE(manager.loadCache(entry, ctx, 0));
}

TEST(KVCacheManagerTest, LRUEviction) {
    KVCacheManager::Config config;
    config.max_entries = 3;
    KVCacheManager manager(config);

    // 插入 4 个条目
    for (int i = 0; i < 4; i++) {
        std::vector<int> tokens(10, i);
        manager.saveCache(tokens, "test", ctx, 0);
    }

    // 应该只保留 3 个
    auto stats = manager.getStats();
    ASSERT_EQ(stats.current_entries, 3);
    ASSERT_EQ(stats.evictions_lru, 1);
}

TEST(KVCacheManagerTest, PrefixMatching) {
    KVCacheManager manager;

    // 保存前缀 [1,2,3,4,5]
    std::vector<int> prefix = {1,2,3,4,5};
    manager.saveCache(prefix, "prefix", ctx, 0);

    // 查找 [1,2,3,4,5,6,7]（包含前缀）
    std::vector<int> query = {1,2,3,4,5,6,7};
    auto entry = manager.findLongestPrefix(query);

    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->token_count, 5);
}
```

### 2. 性能基准测试

```cpp
// benchmark/cache_benchmark.cpp

void BM_CacheSave(benchmark::State& state) {
    KVCacheManager manager;
    llama_context* ctx = /* ... */;

    for (auto _ : state) {
        std::vector<int> tokens(state.range(0));
        manager.saveCache(tokens, "test", ctx, 0);
    }
}
BENCHMARK(BM_CacheSave)->Range(10, 1000);

void BM_CacheLoad(benchmark::State& state) {
    KVCacheManager manager;
    llama_context* ctx = /* ... */;

    // 预先保存
    std::vector<int> tokens(state.range(0));
    manager.saveCache(tokens, "test", ctx, 0);
    auto entry = manager.findLongestPrefix(tokens);

    for (auto _ : state) {
        manager.loadCache(entry, ctx, 0);
    }
}
BENCHMARK(BM_CacheLoad)->Range(10, 1000);

void BM_PrefixSearch(benchmark::State& state) {
    KVCacheManager manager;

    // 预先插入 1000 个条目
    for (int i = 0; i < 1000; i++) {
        std::vector<int> tokens(100, i);
        manager.saveCache(tokens, "test", ctx, 0);
    }

    for (auto _ : state) {
        std::vector<int> query(100, rand() % 1000);
        manager.findLongestPrefix(query);
    }
}
BENCHMARK(BM_PrefixSearch);
```

---

## 📝 待解决的问题

### 1. llama.cpp API 细节
- [ ] 确认 `llama_state_seq_get_data()` 的确切用法
- [ ] 确认状态大小是否固定还是动态
- [ ] 确认多 sequence 的隔离机制

### 2. 性能优化
- [ ] 状态压缩算法选择（zstd? lz4?）
- [ ] 前缀树是否需要优化（Patricia Trie?）
- [ ] 并发性能瓶颈分析

### 3. 策略调优
- [ ] 最佳 `min_prefix_tokens` 值
- [ ] 最佳 `prefix_match_threshold` 值
- [ ] LRU vs LFU vs Mixed 哪个更好

---

## 🚀 下一步行动

1. **Week 1**: 研究 llama.cpp API，验证设计假设
2. **Week 2**: 实现核心功能（save, load, find）
3. **Week 3**: 实现淘汰策略和统计
4. **Week 4**: 性能测试与优化

---

**版本历史**:
- v2.0 (2025-11-10): 初始设计

**作者**: AI-Chats Team
