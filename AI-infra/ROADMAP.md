# AI-Chats 开发路线图

**项目**: 边缘推理共享节点与 KV-Cache 优化
**当前版本**: v0.2.0 (Docker MVP)
**最后更新**: 2025-11-10

---

## 📊 当前状态评估

### ✅ 已完成
- [x] Mac 版本基础功能（kqueue, HTTP 服务器）
- [x] llama.cpp 集成与推理功能
- [x] 用户认证系统（注册/登录）
- [x] 基础 KV-Cache 实现
- [x] 前缀树缓存结构（已实现但暂时禁用）
- [x] Linux 适配（epoll 替换 kqueue）
- [x] llama.cpp API 迁移（新版 API）
- [x] Docker 容器化部署
- [x] 完整部署文档

### ⚠️ 待修复
- [ ] KV-Cache 前缀缓存功能（因 API 变更被禁用）
- [ ] Warmup 缓存预热（因 API 变更被禁用）
- [ ] 性能监控缺失
- [ ] 缓存命中率统计

### 🎯 研究目标（毕设）
基于开题报告，核心研究方向：
1. **KV-Cache 前缀缓存优化**（方案一）
2. **推测式解码**（方案二，备选）
3. **边缘节点推理共享**

---

## 🚀 开发路线图

### 阶段 1: 核心功能修复（1-2 周）

**目标**: 恢复并增强 KV-Cache 功能

#### 1.1 研究新 llama.cpp KV-Cache API (3天)
**优先级**: 🔥 高

**任务**:
- [ ] 阅读 llama.cpp 最新文档和源码
- [ ] 理解新的 KV-Cache 管理 API
  - `llama_kv_cache_view_init()`
  - `llama_kv_cache_view_free()`
  - `llama_kv_cache_view_update()`
  - `llama_state_get_data()` / `llama_state_set_data()`
- [ ] 编写 API 测试代码，验证理解
- [ ] 记录 API 变化对比文档

**产出**:
- `research/llama-cpp-kv-api-analysis.md`
- `tests/kv_cache_api_test.cpp`

**验收标准**:
- 能够手动保存/恢复 KV-Cache 状态
- 理解新旧 API 的差异

---

#### 1.2 重新实现 KV-Cache 前缀缓存 (5天)
**优先级**: 🔥 高

**任务**:
- [ ] 设计新的 KV-Cache 管理策略
  - 保留原有前缀树结构
  - 适配新 API 的状态保存/恢复机制
- [ ] 实现核心功能
  - [ ] `savePrefixCache()` - 使用新 API 保存状态
  - [ ] `loadPrefixCache()` - 使用新 API 加载状态
  - [ ] `findPrefixCache()` - 查找最长公共前缀
  - [ ] `clearPrefixCache()` - 清理过期缓存
- [ ] 实现缓存淘汰策略（LRU）
- [ ] 添加缓存大小限制

**技术方案**:
```cpp
// 新的 KV-Cache 存储结构
struct KVCacheEntry {
    std::string prefix;           // 前缀内容
    std::vector<uint8_t> state;   // llama_state_get_data() 返回的状态
    size_t state_size;            // 状态大小
    int token_count;              // token 数量
    time_t last_used;             // 最后使用时间（LRU）
};

class KVCacheManager {
public:
    // 保存当前上下文状态为缓存
    bool saveCache(const std::string& prefix, llama_context* ctx);

    // 加载缓存状态到上下文
    bool loadCache(const std::string& prefix, llama_context* ctx);

    // 查找最长公共前缀
    std::string findLongestPrefix(const std::string& prompt);

    // LRU 淘汰
    void evictLRU(size_t target_size);
};
```

**产出**:
- `src/inference/KVCacheManager.cpp`
- `include/inference/KVCacheManager.h`
- 单元测试

**验收标准**:
- 缓存保存/加载成功率 100%
- 前缀匹配准确率 100%
- 内存使用合理（可配置上限）

---

#### 1.3 恢复 Warmup 预热功能 (2天)
**优先级**: 🟡 中

**任务**:
- [ ] 使用新 API 重新实现 warmup
- [ ] 支持批量模板预热
- [ ] 添加预热进度显示

**产出**:
- 更新 `ModelManager::warmupCache()`
- 预热配置文件优化

**验收标准**:
- 服务器启动时成功预热 5+ 常用模板
- 预热后缓存命中率显著提升

---

#### 1.4 性能监控与指标收集 (2天)
**优先级**: 🟡 中

**任务**:
- [ ] 添加性能指标
  - 缓存命中率
  - 平均推理时间
  - Token 生成速度 (tokens/s)
  - 缓存大小统计
- [ ] 实现 `/metrics` 端点（Prometheus 格式）
- [ ] 添加实时日志输出

**技术方案**:
```cpp
struct PerformanceMetrics {
    // 缓存统计
    uint64_t total_requests = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;

    // 性能统计
    double avg_latency_ms = 0.0;
    double avg_tokens_per_sec = 0.0;

    // 缓存状态
    size_t cache_entries = 0;
    size_t cache_size_bytes = 0;

    // 计算缓存命中率
    double getCacheHitRate() const {
        return total_requests > 0 ?
            (double)cache_hits / total_requests : 0.0;
    }
};
```

**产出**:
- `src/monitoring/Metrics.cpp`
- `/metrics` API 端点
- 实时性能日志

**验收标准**:
- 能够实时查看缓存命中率
- 能够导出 Prometheus 格式指标

---

### 阶段 2: 性能优化与测试（2-3 周）

**目标**: 验证优化效果，收集实验数据

#### 2.1 基准测试框架 (3天)
**优先级**: 🔥 高

**任务**:
- [ ] 设计测试场景
  - 单轮对话
  - 多轮对话（测试缓存复用）
  - 代码修改场景（相似前缀）
  - 并发请求
- [ ] 实现自动化测试脚本
- [ ] 收集性能指标

**测试场景示例**:
```python
# benchmark/scenarios.py

# 场景1: 代码修改任务（高前缀重复率）
code_modification_prompts = [
    "给这段Python代码添加注释：\ndef fib(n): ...",
    "给这段Python代码添加类型提示：\ndef fib(n): ...",
    "给这段Python代码优化性能：\ndef fib(n): ...",
]

# 场景2: 多轮对话
multi_turn_conversation = [
    "什么是机器学习？",
    "它和深度学习有什么区别？",  # 延续上文
    "能举个深度学习的例子吗？",   # 延续上文
]

# 场景3: 模板复用
template_reuse = [
    "<|system|>\n你是Python专家\n<|user|>\n...",
    "<|system|>\n你是Python专家\n<|user|>\n...",  # 相同系统提示
]
```

**产出**:
- `benchmark/test_suite.py`
- `benchmark/scenarios/`
- 测试报告模板

**验收标准**:
- 能够自动运行全部测试场景
- 生成详细性能报告（命中率、延迟、吞吐量）

---

#### 2.2 对比实验（有缓存 vs 无缓存）(5天)
**优先级**: 🔥 高

**任务**:
- [ ] 设计 A/B 测试
  - Group A: 启用 KV-Cache 前缀缓存
  - Group B: 禁用缓存
- [ ] 收集性能数据
  - 推理延迟减少百分比
  - 缓存命中率
  - 内存开销
- [ ] 分析不同场景下的效果
  - 代码修改场景
  - 多轮对话场景
  - 单次查询场景

**数据收集**:
| 场景 | 无缓存延迟 | 有缓存延迟 | 命中率 | 加速比 |
|------|-----------|-----------|--------|--------|
| 代码修改 | ?ms | ?ms | ?% | ?x |
| 多轮对话 | ?ms | ?ms | ?% | ?x |
| 单次查询 | ?ms | ?ms | ?% | ?x |

**产出**:
- `research/cache-performance-analysis.md`
- 性能对比图表
- 毕设实验数据

**验收标准**:
- 在高重复场景下，缓存命中率 > 60%
- 命中时延迟减少 > 30%
- 数据可用于毕设论文

---

#### 2.3 缓存策略优化 (3天)
**优先级**: 🟡 中

**任务**:
- [ ] 实验不同淘汰策略
  - LRU (Least Recently Used)
  - LFU (Least Frequently Used)
  - TTL (Time To Live)
  - 混合策略
- [ ] 调优缓存大小
  - 测试不同缓存容量的影响
  - 找到最佳容量/命中率平衡点
- [ ] 前缀匹配优化
  - 测试不同最小前缀长度阈值
  - 优化前缀树查找性能

**产出**:
- 策略对比报告
- 最佳配置参数

**验收标准**:
- 确定生产环境推荐配置
- 不同策略的优劣分析

---

#### 2.4 压力测试与稳定性 (3天)
**优先级**: 🟡 中

**任务**:
- [ ] 并发压力测试
  - 10 并发用户
  - 50 并发用户
  - 100 并发用户
- [ ] 长时间运行测试（24小时）
- [ ] 内存泄漏检测
- [ ] 错误恢复测试

**工具**:
```bash
# 使用 Apache Bench
ab -n 1000 -c 50 -p request.json \
  -T application/json http://localhost:8081/infer

# 使用 wrk
wrk -t4 -c100 -d30s --latency \
  -s post.lua http://localhost:8081/infer
```

**产出**:
- 压力测试报告
- 性能瓶颈分析
- 稳定性改进建议

**验收标准**:
- 100 并发下服务稳定运行
- 24 小时运行无崩溃
- 内存使用稳定

---

### 阶段 3: 高级特性（3-4 周）

**目标**: 实现边缘推理共享和高级优化

#### 3.1 分布式 KV-Cache 共享（核心创新）(7天)
**优先级**: 🔥 高（毕设核心）

**研究问题**:
- 多个边缘节点如何共享 KV-Cache？
- 如何解决缓存一致性问题？
- 网络传输开销 vs 计算节省的权衡？

**技术方案选择**:

**方案 A: 中心化缓存服务**
```
┌─────────┐    ┌─────────┐    ┌─────────┐
│ Node 1  │───▶│  Redis  │◀───│ Node 2  │
│(Infer)  │    │ (Cache) │    │(Infer)  │
└─────────┘    └─────────┘    └─────────┘
```

**方案 B: P2P 缓存共享**
```
┌─────────┐
│ Node 1  │◀──────────┐
│(Infer)  │           │
└────┬────┘           │
     │                │
     │    ┌─────────┐ │
     └───▶│ Node 2  │─┘
          │(Infer)  │
          └─────────┘
```

**任务**:
- [ ] 设计缓存共享协议
- [ ] 实现缓存序列化/反序列化
- [ ] 实现节点间通信
- [ ] 缓存同步策略
- [ ] 一致性保证机制

**技术挑战**:
1. **序列化开销**: KV-Cache 状态可能很大（几十 MB）
2. **网络延迟**: 传输时间 vs 重新计算时间
3. **一致性**: 多节点并发写入
4. **安全性**: 缓存数据加密

**产出**:
- 分布式缓存原型
- 性能对比分析
- 毕设核心章节

**验收标准**:
- 成功实现跨节点缓存共享
- 网络传输 + 加载时间 < 重新推理时间
- 缓存命中率进一步提升

---

#### 3.2 推测式解码（备选方案）(7天)
**优先级**: 🟡 中（作为对比实验）

**原理**:
使用小模型预测 draft tokens，大模型验证并修正

```
Small Model (draft):  "The cat sat on the"
                              ↓
Large Model (verify): "The cat sat on the mat"
                       ✓   ✓   ✓   ✓   ✓   ✗→修正
```

**任务**:
- [ ] 集成小模型（如 TinyLlama-160M）
- [ ] 实现 draft-verify 流程
- [ ] 性能对比实验

**产出**:
- 推测式解码实现
- 与 KV-Cache 方案的对比
- 毕设对比实验

**验收标准**:
- 成功实现推测式解码
- 收集加速比数据
- 与 KV-Cache 方案对比分析

---

#### 3.3 前端可视化界面（可选）(5天)
**优先级**: 🟢 低

**任务**:
- [ ] 设计 Web UI
  - 聊天界面
  - 性能监控面板
  - 缓存状态可视化
- [ ] 实现实时指标展示
- [ ] 添加配置管理界面

**技术栈**:
- React / Vue.js
- Chart.js / ECharts（性能图表）
- WebSocket（实时更新）

**产出**:
- Web 管理界面
- 实时性能监控

---

### 阶段 4: 毕设论文与总结（2-3 周）

**目标**: 整理研究成果，撰写论文

#### 4.1 数据整理与分析 (5天)
**优先级**: 🔥 高

**任务**:
- [ ] 汇总所有实验数据
- [ ] 生成对比图表
  - 缓存命中率曲线
  - 延迟对比柱状图
  - 吞吐量对比
- [ ] 统计分析
  - 平均值、中位数、P95、P99
  - 标准差分析
  - 显著性检验

**产出**:
- 实验数据集
- 可视化图表
- 统计分析报告

---

#### 4.2 论文撰写 (10天)
**优先级**: 🔥 高

**章节规划**:
1. **引言**（已有开题报告基础）
2. **相关工作**
   - KV-Cache 优化研究综述
   - 边缘计算推理
   - 推测式解码
3. **系统设计**
   - 整体架构
   - KV-Cache 管理
   - 分布式缓存共享（创新点）
4. **实现细节**
   - 技术选型
   - 关键算法
   - 优化策略
5. **实验评估**
   - 实验设置
   - 性能对比
   - 结果分析
6. **总结与展望**

**产出**:
- 完整毕设论文
- 答辩 PPT

---

#### 4.3 代码整理与文档 (3天)
**优先级**: 🟡 中

**任务**:
- [ ] 代码注释完善
- [ ] API 文档生成（Doxygen）
- [ ] 用户手册更新
- [ ] 开发者文档

**产出**:
- 规范的代码仓库
- 完整文档
- README 更新

---

## 📅 时间规划（总计 8-10 周）

```
Week 1-2:  阶段1 - 核心功能修复
  ├─ Week 1: KV-Cache API 研究 + 重新实现
  └─ Week 2: Warmup 恢复 + 性能监控

Week 3-5:  阶段2 - 性能优化与测试
  ├─ Week 3: 基准测试 + 对比实验
  ├─ Week 4: 缓存策略优化
  └─ Week 5: 压力测试

Week 6-9:  阶段3 - 高级特性
  ├─ Week 6-7: 分布式 KV-Cache 共享（核心）
  ├─ Week 8: 推测式解码（备选）
  └─ Week 9: 前端界面（可选）

Week 10-12: 阶段4 - 论文撰写
  ├─ Week 10: 数据整理
  ├─ Week 11-12: 论文撰写 + 答辩准备
  └─ Week 12: 代码整理 + 文档完善
```

---

## 🎯 里程碑

### M1: 核心功能恢复 (Week 2)
- [x] Docker 部署完成
- [ ] KV-Cache 功能正常
- [ ] 性能监控上线
- [ ] 基础测试通过

### M2: 性能优化完成 (Week 5)
- [ ] 完整基准测试
- [ ] 缓存命中率 > 60%（高重复场景）
- [ ] 延迟减少 > 30%（命中时）
- [ ] 实验数据充足

### M3: 创新功能完成 (Week 9)
- [ ] 分布式缓存共享实现
- [ ] 性能对比实验完成
- [ ] 技术方案验证

### M4: 毕设完成 (Week 12)
- [ ] 论文初稿完成
- [ ] 代码整理完成
- [ ] 答辩准备就绪

---

## 🔬 研究指标

### 核心指标
1. **缓存命中率**
   - 目标: > 60% (代码修改场景)
   - 目标: > 40% (多轮对话场景)

2. **延迟减少**
   - 目标: 命中时延迟减少 > 30%
   - 目标: 端到端延迟减少 > 20%

3. **吞吐量提升**
   - 目标: QPS 提升 > 50%

4. **内存开销**
   - 目标: 缓存内存 < 2GB
   - 目标: 平均每条缓存 < 50MB

### 创新点指标（分布式缓存）
1. **跨节点命中率**
   - 目标: > 30%

2. **传输效率**
   - 目标: 传输 + 加载时间 < 重新推理时间

3. **扩展性**
   - 目标: 支持 3+ 节点

---

## 💡 风险与应对

### 风险 1: llama.cpp API 学习曲线
**影响**: 可能延迟 1 周
**应对**:
- 预留充足学习时间
- 参考官方示例和社区讨论
- 如无法适配，降级到旧版 llama.cpp

### 风险 2: 分布式缓存实现复杂
**影响**: 可能延迟 2 周
**应对**:
- 先实现简化版（中心化 Redis）
- 如时间不足，降级为单机优化

### 风险 3: 性能提升不明显
**影响**: 影响论文说服力
**应对**:
- 调整测试场景，寻找最佳适用场景
- 增加对比实验，分析不同场景下的效果
- 如效果不佳，转向推测式解码

### 风险 4: 时间不足
**影响**: 部分功能未完成
**应对**:
- 优先保证核心功能（KV-Cache）
- 砍掉可选功能（前端界面）
- 分布式缓存改为设计方案（不实现）

---

## 📚 学习资源

### llama.cpp
- 官方仓库: https://github.com/ggerganov/llama.cpp
- 示例代码: `examples/` 目录
- API 文档: `llama.h` 头文件注释

### KV-Cache 优化
- 论文: "Fast Transformer Decoding" (Google)
- 论文: "FlashAttention" (Stanford)

### 分布式缓存
- Redis 官方文档
- gRPC / ZeroMQ（节点通信）

### 推测式解码
- 论文: "Fast Inference from Transformers via Speculative Decoding"
- 论文: "Medusa: Multiple Decoding Heads"

---

## 📝 下周行动计划

### Week 1 目标: 研究 llama.cpp KV-Cache API

**Day 1-2**:
- [ ] 阅读 llama.cpp 源码（`llama.h`, `llama.cpp`）
- [ ] 查找 KV-Cache 相关函数
- [ ] 阅读官方示例代码

**Day 3-4**:
- [ ] 编写简单测试程序
- [ ] 验证状态保存/恢复
- [ ] 记录笔记

**Day 5**:
- [ ] 设计新的 KVCacheManager 接口
- [ ] 编写技术方案文档
- [ ] 评估工作量

**产出**:
- `research/llama-cpp-kv-api-analysis.md`
- `design/kv-cache-manager-v2.md`

---

## ✅ 检查清单

每周结束时检查：
- [ ] 本周计划完成度 > 80%
- [ ] 关键代码已提交 Git
- [ ] 实验数据已备份
- [ ] 文档已更新
- [ ] 下周计划已制定

---

**版本**: v1.0
**创建日期**: 2025-11-10
**负责人**: [你的名字]
**导师**: [导师名字]
