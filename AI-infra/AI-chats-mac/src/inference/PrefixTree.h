#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>
#include <limits>

// ============================================================
// Prefix Tree (Trie) for KV-Cache Management
// ============================================================
// 使用 Trie 数据结构实现前缀缓存的部分匹配功能
// 相比哈希表的精确匹配，Trie 可以找到最长公共前缀，提升缓存命中率
//
// 示例：
//   插入: [1, 2, 3, 4, 5] -> seq_id=1
//   插入: [1, 2, 6, 7]    -> seq_id=2
//   查询: [1, 2, 3, 4, 5] -> 命中 seq_id=1 (完全匹配)
//   查询: [1, 2, 6, 7]    -> 命中 seq_id=2 (完全匹配)
//   查询: [1, 2, 8]       -> 部分命中，匹配到 [1,2]，可复用部分KV cache
// ============================================================

// Trie 树节点
struct TrieNode {
    // 子节点映射：token_id -> TrieNode指针
    std::unordered_map<int, std::shared_ptr<TrieNode>> children;

    // 如果此节点是某个缓存前缀的终点，保存相关信息
    int seq_id = -1;           // llama.cpp序列ID（-1表示非缓存终点）
    int kv_length = 0;         // 缓存的token数量
    uint64_t last_used_ns = 0; // 最后使用时间（纳秒）
    uint32_t hit_count = 0;    // 命中次数

    // 判断是否是缓存终点
    bool is_cached() const {
        return seq_id >= 0;
    }

    // 获取当前时间（纳秒）
    static uint64_t getCurrentTimeNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
};

// Prefix Tree 前缀缓存管理器
class PrefixTree {
public:
    // 最小有效前缀长度（tokens）
    // 小于此长度的匹配将被忽略，避免过短的前缀缓存收益不明显
    static constexpr size_t MIN_USEFUL_PREFIX_LEN = 8;

    PrefixTree() : root_(std::make_shared<TrieNode>()), entry_count_(0) {}

    // ============================================================
    // 核心接口
    // ============================================================

    // 查找最长匹配前缀
    // @param tokens: 待查询的token序列
    // @return: <seq_id, 匹配的token数量>
    //          seq_id=-1 表示完全未命中
    //          seq_id>=0 表示命中，matched_len 是匹配的token数量
    std::pair<int, int> findLongestPrefix(const std::vector<int>& tokens);

    // 插入新的前缀缓存
    // @param tokens: token序列
    // @param seq_id: llama.cpp中对应的序列ID
    void insert(const std::vector<int>& tokens, int seq_id);

    // 更新命中统计（在缓存命中时调用）
    // @param tokens: 命中的token序列
    void updateHit(const std::vector<int>& tokens);

    // LRU淘汰：删除最久未使用的缓存条目
    // @param max_entries: 最大缓存条目数
    // @return: 被淘汰的 seq_id 列表
    std::vector<int> evictLRU(size_t max_entries);

    // 清空所有缓存
    void clear() {
        root_ = std::make_shared<TrieNode>();
        entry_count_ = 0;
    }

    // 获取当前缓存条目数量
    size_t size() const {
        return entry_count_;
    }

private:
    std::shared_ptr<TrieNode> root_;  // Trie树根节点
    size_t entry_count_;              // 当前缓存条目数

    // 辅助函数：递归查找最旧的叶子节点
    // @return: <节点指针, 最后使用时间>
    std::pair<TrieNode*, uint64_t> findOldestLeaf(TrieNode* node);

    // 辅助函数：递归删除指定节点
    bool removeNode(TrieNode* parent, TrieNode* target);
};
