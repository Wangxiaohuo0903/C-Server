# 📋 本周任务清单

**周期**: Week 1 (2025-11-11 ~ 2025-11-17)
**目标**: 研究 llama.cpp 新 KV-Cache API，设计迁移方案

---

## 🎯 本周目标

1. 深入理解 llama.cpp 最新 KV-Cache API
2. 设计新的 KVCacheManager 实现方案
3. 编写 API 测试代码验证理解
4. 为下周的实现做好准备

---

## 📅 日程安排

### Day 1 (周一) - llama.cpp 源码学习

#### 🔍 阅读源码
- [ ] 下载最新 llama.cpp 源码
  ```bash
  cd third_party
  git clone https://github.com/ggerganov/llama.cpp.git
  cd llama.cpp
  git log --oneline | head -20  # 查看最近更新
  ```

- [ ] 阅读核心文件（重点关注 KV-Cache）
  - [ ] `llama.h` (API 定义) - 重点看注释
  - [ ] `llama.cpp` (实现) - 搜索 `kv_cache` 相关函数
  - [ ] `common/common.h` (工具函数)

- [ ] 记录关键 API
  ```cpp
  // 在笔记中记录以下函数的签名和用途:

  // KV-Cache 管理
  llama_kv_cache_view_init()
  llama_kv_cache_view_free()
  llama_kv_cache_view_update()
  llama_kv_cache_clear()
  llama_kv_cache_seq_rm()
  llama_kv_cache_seq_cp()
  llama_kv_cache_seq_keep()
  llama_kv_cache_seq_add()
  llama_kv_cache_seq_div()

  // 状态保存/恢复
  llama_state_get_size()
  llama_state_get_data()
  llama_state_set_data()
  llama_state_seq_get_size()
  llama_state_seq_get_data()
  llama_state_seq_set_data()
  llama_state_seq_save_file()
  llama_state_seq_load_file()
  ```

#### 📖 查找文档和示例
- [ ] 阅读官方示例
  - `examples/save-load-state/` - 状态保存示例
  - `examples/main/` - 基础推理示例
  - `examples/server/` - 服务器实现

- [ ] 搜索相关 Issue 和 PR
  - 在 GitHub 搜索: `kv cache save`
  - 在 GitHub 搜索: `state save load`
  - 查看最近的 API 变更

#### 📝 产出
- [ ] 创建笔记文件 `research/day1-llama-api-notes.md`
- [ ] 记录所有 KV-Cache 相关 API
- [ ] 记录疑问和待验证的点

**预计时间**: 6-8 小时

---

### Day 2 (周二) - API 对比分析

#### 🔄 旧 API vs 新 API 对比
- [ ] 整理旧代码中使用的 API
  ```cpp
  // 旧版 API (已废弃)
  llama_kv_cache_clear(ctx)
  llama_kv_cache_seq_rm(ctx, seq_id, p0, p1)
  llama_kv_cache_seq_cp(ctx, seq_id_src, seq_id_dst, p0, p1)
  ```

- [ ] 找到新 API 的等价功能
  ```markdown
  | 旧 API | 新 API | 变化说明 |
  |--------|--------|----------|
  | llama_kv_cache_clear | ? | ? |
  | llama_kv_cache_seq_rm | ? | ? |
  | ... | ... | ... |
  ```

#### 🧪 编写测试代码
- [ ] 创建测试项目
  ```bash
  mkdir -p tests/kv_cache_test
  cd tests/kv_cache_test
  touch main.cpp CMakeLists.txt
  ```

- [ ] 编写简单的保存/加载测试
  ```cpp
  // tests/kv_cache_test/main.cpp
  #include "llama.h"

  int main() {
      // 1. 加载模型
      // 2. 初始化上下文
      // 3. 进行一次推理
      // 4. 保存 KV-Cache 状态
      // 5. 清空上下文
      // 6. 恢复 KV-Cache 状态
      // 7. 验证是否恢复成功
  }
  ```

#### 📝 产出
- [ ] API 对比表格（Markdown）
- [ ] 测试代码（可编译运行）
- [ ] 运行结果记录

**预计时间**: 6-8 小时

---

### Day 3 (周三) - 深入理解 Sequence 机制

#### 🔍 研究 Sequence 概念
llama.cpp 使用 sequence 来管理多轮对话和并发请求

- [ ] 理解 Sequence ID 的作用
  - 每个对话/请求分配一个 seq_id
  - KV-Cache 按 seq_id 隔离

- [ ] 研究 Sequence 操作
  ```cpp
  // 复制 sequence (用于分支对话)
  llama_kv_cache_seq_cp(ctx, src_seq, dst_seq, p0, p1)

  // 删除 sequence (清理过期对话)
  llama_kv_cache_seq_rm(ctx, seq_id, p0, p1)

  // 保留 sequence (淘汰其他)
  llama_kv_cache_seq_keep(ctx, seq_id)
  ```

- [ ] 设计我们的 Sequence 分配策略
  - 如何为每个 chat_id 分配 seq_id？
  - 如何管理 seq_id 的生命周期？

#### 🧪 实验 Sequence 功能
- [ ] 修改测试代码，测试多 sequence
  ```cpp
  // 测试场景：
  // 1. 创建 seq_id=0，推理 "Hello"
  // 2. 复制到 seq_id=1
  // 3. seq_id=0 继续 "How are you?"
  // 4. seq_id=1 继续 "What's your name?"
  // 5. 验证两个分支独立
  ```

#### 📝 产出
- [ ] Sequence 机制说明文档
- [ ] Sequence 测试代码
- [ ] 应用于我们项目的设计草案

**预计时间**: 6-8 小时

---

### Day 4 (周四) - 状态序列化研究

#### 💾 研究状态保存/恢复
- [ ] 理解状态的内容
  - KV-Cache 包含什么数据？
  - 状态大小估算

- [ ] 测试状态保存
  ```cpp
  // 获取所需大小
  size_t state_size = llama_state_get_size(ctx);

  // 分配缓冲区
  std::vector<uint8_t> state_data(state_size);

  // 保存状态
  llama_state_get_data(ctx, state_data.data(), state_size);

  // 恢复状态
  llama_state_set_data(ctx, state_data.data(), state_size);
  ```

- [ ] 测试 Sequence 级别的保存
  ```cpp
  // 只保存特定 sequence
  size_t seq_size = llama_state_seq_get_size(ctx, seq_id);
  std::vector<uint8_t> seq_data(seq_size);
  llama_state_seq_get_data(ctx, seq_data.data(), seq_id);
  ```

#### 📊 性能测试
- [ ] 测量状态大小
  - 不同 token 数量对应的状态大小
  - 压缩前/后大小对比

- [ ] 测量序列化时间
  - 保存时间
  - 加载时间

#### 📝 产出
- [ ] 状态序列化代码示例
- [ ] 性能数据表格
- [ ] 优化建议

**预计时间**: 6-8 小时

---

### Day 5 (周五) - 设计新架构

#### 🏗️ 设计 KVCacheManager v2
基于前 4 天的理解，设计新的实现

- [ ] 设计类接口
  ```cpp
  class KVCacheManager {
  public:
      // 保存当前上下文的 KV-Cache
      bool saveCache(
          const std::string& cache_key,
          llama_context* ctx,
          int seq_id
      );

      // 加载 KV-Cache 到上下文
      bool loadCache(
          const std::string& cache_key,
          llama_context* ctx,
          int seq_id
      );

      // 查找最长公共前缀
      std::string findLongestPrefix(
          const std::string& prompt
      );

      // LRU 淘汰
      void evictLRU(size_t max_entries);

  private:
      struct CacheEntry {
          std::string prefix;
          std::vector<uint8_t> state_data;
          size_t token_count;
          time_t last_accessed;
      };

      std::map<std::string, CacheEntry> cache_;
      std::mutex mutex_;
  };
  ```

- [ ] 设计数据流程
  ```
  用户请求 → 查找前缀 → 命中？
     ↓ 是                    ↓ 否
  加载缓存                从头推理
     ↓                       ↓
  继续推理                保存缓存
     ↓                       ↓
  返回结果 ←─────────────── ┘
  ```

- [ ] 设计配置参数
  ```yaml
  kv_cache:
    enabled: true
    max_entries: 100        # 最大缓存条目
    max_memory_mb: 1024     # 最大内存使用
    min_prefix_tokens: 10   # 最小前缀长度
    ttl_seconds: 3600       # 缓存过期时间
  ```

#### 📋 制定实现计划
- [ ] 拆分为子任务
  - Week 2-3: 核心功能实现
  - Week 4: 测试与优化

- [ ] 评估风险
  - 技术难点
  - 时间估算

#### 📝 产出
- [ ] `design/kv-cache-manager-v2.md` 设计文档
- [ ] `design/implementation-plan.md` 实现计划
- [ ] Week 2 任务清单

**预计时间**: 6-8 小时

---

## 📊 本周产出检查清单

### 文档
- [ ] `research/day1-llama-api-notes.md` - API 笔记
- [ ] `research/api-comparison.md` - 新旧 API 对比
- [ ] `research/sequence-mechanism.md` - Sequence 机制说明
- [ ] `research/state-serialization.md` - 状态序列化研究
- [ ] `design/kv-cache-manager-v2.md` - 设计文档
- [ ] `design/implementation-plan.md` - 实现计划

### 代码
- [ ] `tests/kv_cache_test/` - API 测试代码
- [ ] 所有测试都能成功运行
- [ ] 记录测试结果和性能数据

### 理解
- [ ] 能够解释新 API 的工作原理
- [ ] 能够说明如何将旧代码迁移到新 API
- [ ] 确定下周实现的技术路线

---

## 💡 学习资源

### 官方资源
- **llama.cpp GitHub**: https://github.com/ggerganov/llama.cpp
- **示例代码**: `examples/` 目录
- **头文件**: `llama.h` (最权威的文档)

### 相关 Issues（可能有用）
- 搜索: `kv cache save load`
- 搜索: `state serialization`
- 查看最近 6 个月的 merged PR

### 建议阅读顺序
1. `llama.h` - 看所有 `llama_kv_*` 和 `llama_state_*` 函数
2. `examples/save-load-state/` - 理解基础用法
3. `examples/server/server.cpp` - 看生产级代码
4. `llama.cpp` - 深入理解实现细节

---

## ⚠️ 注意事项

1. **不要急于编码**
   - 本周重点是理解，不是实现
   - 充分理解后再动手可以避免返工

2. **记录所有疑问**
   - 不确定的地方先记下来
   - 可以在社区/GitHub 提问

3. **保持代码可运行**
   - 每个测试都要能跑起来
   - 验证你的理解是否正确

4. **时间管理**
   - 每天 6-8 小时有效学习时间
   - 如果卡住超过 2 小时，换个方向

---

## 📞 需要帮助时

### 遇到技术问题
1. 查看 llama.cpp Issues
2. 阅读源码注释
3. 运行官方示例代码

### 理解困难
1. 画流程图/时序图
2. 编写简化版测试代码
3. 对比新旧实现

---

## ✅ 每日检查

每天结束前问自己：
- [ ] 今天的学习目标达成了吗？
- [ ] 产出的文档/代码质量如何？
- [ ] 有哪些疑问需要明天解决？
- [ ] 明天的任务是什么？

---

## 🎯 周五下午总结会议

### 准备事项
- [ ] 整理本周所有产出
- [ ] 梳理下周任务清单
- [ ] 评估进度（是否按计划）
- [ ] 记录经验教训

### 讨论要点
1. 技术理解是否充分？
2. 设计方案是否可行？
3. 下周能否开始实现？
4. 需要调整计划吗？

---

**加油！这周的学习将为后续开发打下坚实基础！** 💪
