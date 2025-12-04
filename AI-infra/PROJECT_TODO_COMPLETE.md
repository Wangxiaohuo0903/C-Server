# 🎯 完整项目待办清单 - 全面视角

**更新时间**: 2025-11-27
**项目状态**: Phase 1 ✅ 完成，Phase 2-4 规划中

---

## 📊 项目全貌

### 已完成的核心模块 ✅

**基础设施层**:
- ✅ HttpServer (22KB) - HTTP服务器实现
- ✅ Router (10KB) - 路由管理
- ✅ ThreadPool (3KB) - 线程池
- ✅ Logger (1.4KB) - 日志系统
- ✅ Database (6KB) - 数据库接口

**推理引擎层**:
- ✅ SpeculativeDecoder (35KB) - 推测式解码核心
- ✅ TaskClassifier (21KB) - 任务分类器
- ✅ ModelManager (28KB) - 模型管理
- ✅ PrefixTree (9KB) - 前缀树优化
- ⏳ ConfidenceGuide (7KB头文件) - 置信度引导

**测试与工具**:
- ✅ test_task_aware.cpp - 完整测试套件
- ✅ test_confidence_guide.cpp - 置信度测试
- ✅ 多个benchmark脚本

**文档**:
- ✅ 100+ Markdown文档
- ✅ 完整论文规划
- ✅ 实验报告

---

## 🎯 核心待办事项 (按优先级)

### 🔥 Phase 2: Token置信度引导 (高优先级)

**目标**: 实现Token级置信度计算和自适应推测策略

#### 2.1 实现ConfidenceGuide.cpp

**任务清单**:
- [ ] **Day 1-2**: 实现核心功能
  - [ ] TokenConfidenceCalculator::calculate() 实现
  - [ ] TokenConfidenceCalculator::calculateEntropy() 实现
  - [ ] TokenConfidenceCalculator::softmax() 实现
  - [ ] ConfidenceGuidedStrategy::adjustDraftSize() 实现
  - [ ] ConfidenceGuidedStrategy::getStatistics() 实现

- [ ] **Day 2-3**: 单元测试
  - [ ] 测试高/中/低置信度token计算
  - [ ] 测试自适应调整逻辑
  - [ ] 测试边界条件
  - [ ] 验证性能开销 (<1ms)

**代码框架**:
```cpp
// src/inference/ConfidenceGuide.cpp
#include "ConfidenceGuide.h"
#include <cmath>
#include <algorithm>
#include <numeric>

// 实现 TokenConfidenceCalculator
float TokenConfidenceCalculator::calculate(
    const std::vector<float>& logits
) {
    // 1. Softmax归一化
    auto probs = softmax(logits);

    // 2. 计算Shannon熵
    float entropy = calculateEntropy(probs);

    // 3. 归一化置信度
    float max_entropy = std::log2(static_cast<float>(logits.size()));
    float confidence = 1.0f - (entropy / max_entropy);

    return std::max(0.0f, std::min(1.0f, confidence));
}

// ... 更多实现
```

**预期输出**:
- ConfidenceGuide.cpp (约200-300行)
- 单元测试通过率100%
- 性能: <1ms per token

#### 2.2 集成到SpeculativeDecoder

**任务清单**:
- [ ] **Day 3-4**: 修改SpeculativeDecoder
  - [ ] 添加 #include "ConfidenceGuide.h"
  - [ ] 添加成员变量 ConfidenceGuidedStrategy confidence_strategy_
  - [ ] 在构造函数中初始化
  - [ ] 在generation loop中添加置信度计算
  - [ ] 添加配置选项 enable_confidence_guide

- [ ] **Day 4**: 编译测试
  - [ ] 修改CMakeLists.txt添加ConfidenceGuide.cpp
  - [ ] 编译通过
  - [ ] 运行test_task_aware验证集成

**修改文件**:
- `AI-chats-linux/src/inference/SpeculativeDecoder.cpp`
- `AI-chats-linux/CMakeLists.txt`

**预期结果**:
- 编译无错误
- 基础功能测试通过
- 置信度计算正常工作

#### 2.3 置信度相关性实验

**任务清单**:
- [ ] **Day 5-6**: 数据收集
  - [ ] 创建数据收集脚本
  - [ ] 运行1000个token样本
  - [ ] 记录每个token的置信度和Accept结果
  - [ ] 保存到CSV文件

- [ ] **Day 6**: 数据分析
  - [ ] 计算Pearson相关系数
  - [ ] 绘制散点图
  - [ ] 分bin统计 (0-0.6, 0.6-0.7, ...)
  - [ ] 生成实验报告

**脚本文件**:
```python
# scripts/analyze_confidence.py
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from scipy import stats

# 读取数据
df = pd.read_csv('confidence_data.csv')

# 计算相关性
correlation, p_value = stats.pearsonr(df['confidence'], df['accepted'])
print(f"Pearson r = {correlation:.3f}, p = {p_value:.4f}")

# 绘制散点图
plt.figure(figsize=(10, 6))
plt.scatter(df['confidence'], df['accepted'], alpha=0.3)
plt.xlabel('Token Confidence')
plt.ylabel('Accept Rate')
plt.title(f'Confidence vs Accept Rate (r={correlation:.3f})')
plt.savefig('confidence_correlation.png', dpi=300)

# 分bin统计
bins = [0, 0.6, 0.7, 0.8, 0.9, 1.0]
df['conf_bin'] = pd.cut(df['confidence'], bins)
grouped = df.groupby('conf_bin').agg({
    'accepted': ['mean', 'count']
})
print(grouped)
```

**预期结果**:
- Pearson相关系数: r > 0.7 (强相关)
- p-value < 0.001 (统计显著)
- 置信度越高，Accept Rate越高
- 论文图表就绪

#### 2.4 阈值策略对比

**任务清单**:
- [ ] **Day 7**: 对比实验
  - [ ] 实现5种阈值配置
  - [ ] 每种配置测试50次
  - [ ] 记录Accept Rate, Speedup, Volatility
  - [ ] 生成对比表格

**配置**:
```cpp
struct ThresholdConfig {
    float high_threshold;
    float low_threshold;
    std::string name;
};

std::vector<ThresholdConfig> configs = {
    {0.95, 0.75, "Very Conservative"},
    {0.90, 0.70, "Conservative"},
    {0.85, 0.65, "Moderate"},
    {0.80, 0.60, "Aggressive"},
    {0.75, 0.55, "Very Aggressive"}
};
```

**预期结果**:
- 找到最优阈值配置 (Moderate: 0.85/0.65)
- 论文表格数据完整

---

### 🚀 Phase 3: 系统集成 (中优先级)

**目标**: 将推测式解码集成到HTTP服务器，提供完整API

#### 3.1 HTTP API端点实现

**任务清单**:
- [ ] 实现 POST /v1/chat/completions
  - [ ] 解析OpenAI格式请求
  - [ ] 调用SpeculativeDecoder
  - [ ] 构建兼容响应

- [ ] 实现 POST /v1/generate
  - [ ] 文本续写生成
  - [ ] 支持推测式解码参数

- [ ] 实现 GET /health
  - [ ] 系统健康检查
  - [ ] 模型加载状态

- [ ] 实现 GET /stats
  - [ ] 性能统计
  - [ ] Accept Rate监控

**修改文件**:
- `AI-chats-linux/src/main.cpp`
- `AI-chats-linux/include/Router.h`

**预期成果**:
- 完整的RESTful API
- OpenAI兼容接口
- 性能监控端点

#### 3.2 端到端测试

**任务清单**:
- [ ] 功能测试
  - [ ] curl测试所有API端点
  - [ ] 验证响应格式
  - [ ] 错误处理测试

- [ ] 性能测试
  - [ ] 单并发性能
  - [ ] 多并发压测 (5, 10, 20)
  - [ ] 延迟分布 (P50, P95, P99)

- [ ] 稳定性测试
  - [ ] 24小时长时间运行
  - [ ] 内存泄漏检测
  - [ ] 错误率统计

**工具**:
- curl - 基础功能测试
- Apache Bench (ab) - 并发压测
- wrk - 高级性能测试

**预期结果**:
- QPS > 50
- P95延迟 < 300ms
- 24小时稳定运行无crash

---

### 🌐 Phase 4: 边缘设备优化 (中优先级)

**目标**: 实现资源监控和自适应调度，支持边缘设备部署

#### 4.1 资源监控模块

**任务清单**:
- [ ] 实现ResourceMonitor类
  - [ ] 内存监控 (total, used, available)
  - [ ] CPU使用率监控
  - [ ] 温度监控 (如果支持)
  - [ ] 进程资源统计

- [ ] 实现监控线程
  - [ ] 定时采集 (每秒)
  - [ ] 异常检测
  - [ ] 日志记录

**代码框架**:
```cpp
// include/ResourceMonitor.h
class ResourceMonitor {
public:
    struct SystemStats {
        size_t total_memory_mb;
        size_t used_memory_mb;
        float memory_usage_percent;
        float cpu_usage_percent;
        float cpu_temperature;
        size_t process_memory_mb;
    };

    SystemStats getCurrentStats();
    void startMonitoring(int interval_ms = 1000);
    void stopMonitoring();

private:
    std::thread monitor_thread_;
    std::atomic<bool> running_{false};
    SystemStats latest_stats_;
};
```

#### 4.2 边缘自适应调度

**任务清单**:
- [ ] 实现EdgeAdaptiveScheduler
  - [ ] 内存压力检测 (>80%)
  - [ ] 温度管理 (>80°C)
  - [ ] 电池管理 (<20%)
  - [ ] 自动降级策略

- [ ] 集成到SpeculativeDecoder
  - [ ] 根据资源状态调整n_draft
  - [ ] 降低batch_size
  - [ ] 减少n_ctx

**自适应策略**:
```cpp
void EdgeAdaptiveScheduler::adapt(SpeculativeDecoder::Config& config) {
    auto stats = monitor_.getCurrentStats();

    // 内存压力
    if (stats.memory_usage_percent > 85.0f) {
        config.n_draft = std::max(4, config.n_draft / 2);
        config.n_ctx = std::min(512, config.n_ctx);
    }

    // CPU温度
    if (stats.cpu_temperature > 80.0f) {
        config.n_draft -= 4;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 电池供电
    if (is_battery_powered_ && battery_percent_ < 20) {
        config.enable_speculative = false;  // 省电模式
    }
}
```

#### 4.3 边缘设备实测

**任务清单**:
- [ ] 树莓派4B测试
  - [ ] 部署完整系统
  - [ ] 测试吞吐量
  - [ ] 内存占用分析
  - [ ] 温度监控
  - [ ] 能耗测试

- [ ] Jetson Nano测试
  - [ ] GPU加速测试
  - [ ] 性能对比

- [ ] Intel NUC测试
  - [ ] 桌面边缘性能

**预期数据**:
| 设备 | Tokens/s | 内存占用 | 功耗 |
|------|---------|---------|------|
| 树莓派4B | 5-8 | <1.5GB | ~6W |
| Jetson Nano | 10-15 | <2GB | ~10W |
| Intel NUC | 15-20 | <2GB | ~15W |

---

### 📝 Phase 5: 论文撰写 (高优先级)

**目标**: 完成毕业论文撰写，准备答辩

#### 5.1 可立即开始的章节

**第2章: 理论基础** (8-10页)
- [ ] **Week 1**: 撰写初稿
  - [ ] 2.1 大语言模型推理原理 (2-3页)
  - [ ] 2.2 推测式解码技术 (3-4页)
  - [ ] 2.3 任务分类与自适应推理 (2-3页)
  - [ ] 2.4 边缘计算与资源管理 (2-3页)

- [ ] **Week 2**: 修改完善
  - [ ] 添加参考文献
  - [ ] 绘制原理图
  - [ ] 审阅润色

**第4章: 推测式解码优化方法** (12-15页)
- [ ] **Week 2-3**: 撰写
  - [ ] 4.1 任务感知推测式解码 (4-5页)
    - [ ] 任务分类器设计
    - [ ] 6种任务优化策略
    - [ ] 实验验证 (已有数据)
  - [ ] 4.2 Prompt工程优化 (3-4页) 🌟
    - [ ] 箭头符号法
    - [ ] 5.2倍提升分析
    - [ ] 推广方法
  - [ ] 4.3 Token置信度引导 (4-5页)
    - [ ] 置信度计算方法
    - [ ] 相关性验证 (待补充数据)
    - [ ] 自适应策略
  - [ ] 4.4 联合优化框架 (1-2页)

**第6章 (部分): 实验评估** (6-8页)
- [ ] **Week 3-4**: 撰写
  - [ ] 6.1 测试环境与配置 (2页)
  - [ ] 6.2 任务感知性能评估 (2-3页)
    - [ ] 分类准确率: 92.3%
    - [ ] 6种任务性能对比
  - [ ] 6.3 Prompt优化评估 (2-3页)
    - [ ] 翻译任务5.2倍提升
    - [ ] 对比实验

#### 5.2 待Phase 2完成后撰写

**第6章 (补充)**: (6-8页)
- [ ] 6.4 置信度引导评估
- [ ] 6.5 联合优化Ablation Study
- [ ] 6.6 边缘设备测试

**第3章: 系统设计** (8-10页)
- [ ] 3.1 需求分析
- [ ] 3.2 系统总体架构
- [ ] 3.3 技术选型

**第5章: 系统实现** (15-18页)
- [ ] 5.1 HTTP服务器实现
- [ ] 5.2 模型管理模块
- [ ] 5.3 推测式解码引擎
- [ ] 5.4-5.6 其他模块

---

### 🔧 工程完善 (低优先级)

#### 代码质量提升

- [ ] 代码规范化
  - [ ] 统一命名风格
  - [ ] 添加详细注释
  - [ ] 移除调试代码

- [ ] 性能优化
  - [ ] Profile性能瓶颈
  - [ ] 优化热点代码
  - [ ] 减少内存分配

- [ ] 错误处理
  - [ ] 完善异常处理
  - [ ] 添加错误恢复
  - [ ] 日志完整性

#### 文档完善

- [ ] **API文档**
  - [ ] OpenAPI/Swagger规范
  - [ ] 请求/响应示例
  - [ ] 错误码说明

- [ ] **部署文档**
  - [ ] Docker镜像构建
  - [ ] Kubernetes部署
  - [ ] 配置参数说明

- [ ] **用户手册**
  - [ ] 快速开始指南
  - [ ] 常见问题FAQ
  - [ ] 故障排查

---

### 🎨 可选增强 (Nice to Have)

#### 高级功能

- [ ] **流式输出**
  - [ ] Server-Sent Events (SSE)
  - [ ] 实时token生成
  - [ ] ChatGPT般的体验

- [ ] **模型热加载**
  - [ ] 无需重启切换模型
  - [ ] LRU缓存策略
  - [ ] 预热机制

- [ ] **分布式部署**
  - [ ] 多节点负载均衡
  - [ ] 模型分片
  - [ ] 高可用架构

#### 实验扩展

- [ ] **更多模型测试**
  - [ ] Llama-2 7B/13B
  - [ ] Mistral 7B
  - [ ] Qwen 7B

- [ ] **更多任务类型**
  - [ ] Summarization
  - [ ] Code Translation
  - [ ] Data Analysis

- [ ] **Benchmark对比**
  - [ ] vs 标准SpecDec
  - [ ] vs Medusa
  - [ ] vs AutoRegressive

---

## 📅 时间规划 (3个月)

### 第1个月: 算法完善与初步实验

**Week 1** (当前):
- [x] Phase 1完成 ✅
- [ ] ConfidenceGuide.cpp实现
- [ ] 集成到SpeculativeDecoder
- [ ] 基础测试通过

**Week 2**:
- [ ] 置信度相关性实验
- [ ] 阈值策略对比
- [ ] 撰写第2章理论基础

**Week 3**:
- [ ] 联合优化实验
- [ ] Ablation Study
- [ ] 撰写第4章部分内容

**Week 4**:
- [ ] 数据整理和分析
- [ ] 第2章、第4章初稿完成
- [ ] 准备中期汇报

### 第2个月: 系统集成与边缘测试

**Week 5-6**:
- [ ] HTTP API完整实现
- [ ] 端到端测试
- [ ] 资源监控模块

**Week 7-8**:
- [ ] 边缘设备部署
- [ ] 性能测试
- [ ] 撰写第3章、第5章

### 第3个月: 论文写作与答辩准备

**Week 9-10**:
- [ ] 完成第1章、第6章、第7章
- [ ] 整理所有图表
- [ ] 第一轮完整审阅

**Week 11-12**:
- [ ] 导师审阅修改
- [ ] 格式调整、校对
- [ ] 准备答辩PPT
- [ ] 最终定稿

---

## 🎯 关键里程碑

### Milestone 1: Phase 2完成 (Week 2)
- ✅ ConfidenceGuide完整实现
- ✅ 置信度相关性验证 (r > 0.7)
- ✅ 论文第2章、第4章初稿

### Milestone 2: 系统集成完成 (Week 6)
- ✅ HTTP API全部端点实现
- ✅ 端到端测试通过
- ✅ QPS > 50, P95 < 300ms

### Milestone 3: 边缘测试完成 (Week 8)
- ✅ 3种设备实测数据
- ✅ 论文第3章、第5章、第6章完成
- ✅ 论文完成度80%

### Milestone 4: 论文定稿 (Week 12)
- ✅ 全文70-80页完成
- ✅ 导师审阅通过
- ✅ 答辩PPT就绪

---

## 💡 优先级建议

### 本周最重要的3件事

1. **实现ConfidenceGuide.cpp** ⭐⭐⭐
   - 时间: 2-3天
   - 重要性: 核心算法
   - 阻塞: Phase 2所有后续任务

2. **运行置信度相关性实验** ⭐⭐⭐
   - 时间: 2-3天
   - 重要性: 论文关键数据
   - 产出: 图表、统计结果

3. **撰写第2章或第4章部分** ⭐⭐
   - 时间: 每天2-3小时
   - 重要性: 论文进度
   - 产出: 4-6页初稿

### 近期不紧急的任务

- 系统集成 (可延后到Week 5)
- 边缘测试 (可延后到Week 7)
- 代码优化 (随时可做)
- 文档完善 (有空时做)

---

## 📊 当前完成度评估

### 代码实现: 约70%
- ✅ 推测式解码核心
- ✅ 任务分类器
- ✅ 模型管理
- ⏳ 置信度引导 (待实现)
- ⏳ 系统集成 (待完善)

### 实验数据: 约50%
- ✅ 任务分类准确率
- ✅ 6种任务性能
- ✅ Prompt优化数据
- ⏳ 置信度相关性 (待补充)
- ⏳ Ablation Study (待补充)
- ⏳ 边缘设备数据 (待补充)

### 论文撰写: 约15%
- ✅ 完整大纲
- ✅ 素材整理
- ⏳ 第2章 (待写)
- ⏳ 第3-7章 (待写)

### 整体进度: 约45%

**评价**: 进度超前，Phase 1成果显著，Prompt优化是意外收获！

---

## 🆘 风险与应对

### 风险1: 置信度相关性不理想
**应对**:
- 调整置信度计算方法
- 尝试其他指标 (Top-k概率)
- 最坏情况: 弱化这部分，突出任务感知和Prompt优化

### 风险2: 时间不够
**应对**:
- 优先完成核心实验 (Phase 2)
- 简化边缘测试 (仅1-2设备)
- 系统集成可选择性完成

### 风险3: 边缘设备资源不足
**应对**:
- 使用虚拟机模拟
- 降低模型规模
- 聚焦算法，淡化设备

---

## ✅ 行动计划 (本周)

### 今天 (Day 1)
- [ ] 8:00-10:00: 实现TokenConfidenceCalculator
- [ ] 10:00-12:00: 实现ConfidenceGuidedStrategy
- [ ] 14:00-16:00: 单元测试
- [ ] 16:00-18:00: 集成到SpeculativeDecoder

### 明天 (Day 2)
- [ ] 8:00-10:00: 编译测试
- [ ] 10:00-12:00: 创建数据收集脚本
- [ ] 14:00-18:00: 运行1000个样本

### 后天 (Day 3)
- [ ] 8:00-12:00: 数据分析
- [ ] 14:00-18:00: 绘制图表、撰写实验报告

### Day 4-5
- [ ] 阈值策略对比实验
- [ ] 整理Phase 2成果

### Day 6-7
- [ ] 开始撰写第2章或第4章
- [ ] 准备中期总结

---

**总结**: 项目基础扎实，当前重点是完成Phase 2 (置信度引导) 和开始论文撰写。三个月内完成高质量毕业论文是可行的！

---

_最后更新: 2025-11-27_
_下一次更新: 完成Phase 2后_
