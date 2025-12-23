#include "PrefixTree.h"
#include <iostream>

// ============================================================
// 查找最长匹配前缀 (改进版：支持部分匹配 + 最小长度阈值)
// ============================================================
std::pair<int, int> PrefixTree::findLongestPrefix(const std::vector<int>& tokens) {
    auto node = root_;
    int matched_len = 0;
    int best_seq_id = -1;
    int best_len = 0;

    // 逐token匹配，记录遇到的最后一个缓存终点
    for (size_t i = 0; i < tokens.size(); i++) {
        int token = tokens[i];

        // 如果当前token无法继续匹配，停止
        if (node->children.find(token) == node->children.end()) {
            // 调试日志：匹配中断
            if (matched_len > 0 && matched_len < MIN_USEFUL_PREFIX_LEN) {
                std::cerr << "[PrefixTree] Path matched " << matched_len
                          << " tokens but stopped (< MIN_USEFUL_PREFIX_LEN="
                          << MIN_USEFUL_PREFIX_LEN << ")\n";
            }
            break;
        }

        // 进入子节点
        node = node->children[token];
        matched_len++;

        // 如果当前节点是缓存终点，且长度满足最小阈值，记录为候选结果
        if (node->is_cached() && matched_len >= static_cast<int>(MIN_USEFUL_PREFIX_LEN)) {
            best_seq_id = node->seq_id;
            best_len = matched_len;
            // 继续匹配，寻找更长的前缀
        }
    }

    // 调试日志：显示匹配结果
    if (best_seq_id >= 0 && best_len < matched_len) {
        std::cerr << "[PrefixTree] Partial match: found cached endpoint at "
                  << best_len << " tokens (total path: " << matched_len << " tokens)\n";
    }

    return {best_seq_id, best_len};
}

// ============================================================
// 插入新的前缀缓存
// ============================================================
void PrefixTree::insert(const std::vector<int>& tokens, int seq_id) {
    auto node = root_;

    // 沿着token序列构建路径
    for (int token : tokens) {
        if (node->children.find(token) == node->children.end()) {
            node->children[token] = std::make_shared<TrieNode>();
        }
        node = node->children[token];
    }

    // 标记为缓存终点
    bool is_new_entry = !node->is_cached();
    node->seq_id = seq_id;
    node->kv_length = static_cast<int>(tokens.size());
    node->last_used_ns = TrieNode::getCurrentTimeNs();
    node->hit_count = 1;

    if (is_new_entry) {
        entry_count_++;
    }
}

// ============================================================
// 更新命中统计
// ============================================================
void PrefixTree::updateHit(const std::vector<int>& tokens) {
    auto node = root_;

    // 沿着token序列查找
    for (int token : tokens) {
        if (node->children.find(token) == node->children.end()) {
            return;  // 未找到
        }
        node = node->children[token];
    }

    // 更新统计信息
    if (node->is_cached()) {
        node->hit_count++;
        node->last_used_ns = TrieNode::getCurrentTimeNs();
    }
}

// ============================================================
// LRU淘汰
// ============================================================
std::vector<int> PrefixTree::evictLRU(size_t max_entries) {
    std::vector<int> evicted_seq_ids;

    while (entry_count_ > max_entries) {
        auto [oldest_node, oldest_time] = findOldestLeaf(root_.get());

        if (oldest_node && oldest_node->is_cached()) {
            // 记录被淘汰的 seq_id
            int evicted_seq_id = oldest_node->seq_id;
            evicted_seq_ids.push_back(evicted_seq_id);

            // 标记为非缓存节点（不删除树结构，只清除缓存信息）
            oldest_node->seq_id = -1;
            oldest_node->kv_length = 0;
            oldest_node->hit_count = 0;
            entry_count_--;

            std::cout << "🗑️  Evicted LRU cache seq_id=" << evicted_seq_id
                      << " (age=" << (TrieNode::getCurrentTimeNs() - oldest_time) / 1000000 << "ms)"
                      << std::endl;
        } else {
            // 无法继续淘汰
            break;
        }
    }

    return evicted_seq_ids;
}

// ============================================================
// 辅助函数：递归查找最旧的叶子节点
// ============================================================
std::pair<TrieNode*, uint64_t> PrefixTree::findOldestLeaf(TrieNode* node) {
    if (!node) {
        return {nullptr, UINT64_MAX};
    }

    TrieNode* oldest = nullptr;
    uint64_t oldest_time = UINT64_MAX;

    // 如果当前节点是缓存终点，作为候选
    if (node->is_cached()) {
        oldest = node;
        oldest_time = node->last_used_ns;
    }

    // 递归检查所有子节点
    for (auto& [token, child] : node->children) {
        auto [child_oldest, child_time] = findOldestLeaf(child.get());
        if (child_time < oldest_time) {
            oldest = child_oldest;
            oldest_time = child_time;
        }
    }

    return {oldest, oldest_time};
}
