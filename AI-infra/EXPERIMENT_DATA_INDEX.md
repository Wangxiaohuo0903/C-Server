# 实验数据与文档索引 - 论文撰写快速查找指南

**创建日期**: 2025-12-02
**用途**: 快速定位所有实验数据、分析报告、测试日志，方便论文撰写

---

## 📊 目录结构

```
[1] 论文大纲与规划
[2] Phase 2 置信度引导实验 (核心数据)
[3] 推测式解码基础实验
[4] 任务感知优化实验
[5] 性能优化与调试记录
[6] 技术设计文档
[7] 测试日志文件
[8] 分析脚本与工具
```

---

## 1️⃣ 论文大纲与规划

### 📄 主要大纲文档

| 文件 | 位置 | 用途 | 状态 |
|------|------|------|------|
| **论文大纲-Phase2修正版.md** | `AI-chats-linux/` | ⭐ **最新版本** - 包含Phase 2置信度引导完整内容 | ✅ 最新 |
| 论文大纲-修正版.md | `AI-chats-linux/` | 修正版 (Phase 2前) | 🔄 已替代 |
| 论文大纲.md | `AI-chats-linux/` | 初始版本 | 🔄 已替代 |

**论文撰写建议**: 使用 `论文大纲-Phase2修正版.md` 作为论文结构蓝图

---

## 2️⃣ Phase 2 置信度引导实验 (⭐ 核心数据)

### 📊 实验数据汇总

| 文档 | 位置 | 内容 | 论文章节 |
|------|------|------|----------|
| **PHASE2_DATA_SUMMARY.md** | `根目录` | ⭐ **数据总览** - 6个数据点的完整统计 | 第4章 实验验证 |
| **PHASE2_COMPARISON_REPORT.md** | `根目录` | 对比实验报告 (置信度引导 vs 基线) | 第4章 对比分析 |
| **PHASE2_CORRELATION_ANALYSIS_REPORT.md** | `根目录` | 相关性分析详细报告 (Pearson r, 回归) | 第4章 相关性分析 |
| PHASE2_CONFIDENCE_GUIDE_COMPLETE.md | `根目录` | 实现完成报告 | 第3章 实现细节 |
| PHASE2_PROGRESS_SUMMARY.md | `根目录` | 进度总结 | 附录 |

### 🧪 实验测试日志

| 日志文件 | 位置 | 数据内容 | 数据点数 |
|---------|------|----------|---------|
| **PHASE2_CONFIDENCE_ENABLED_RESULTS.log** | `根目录` | ⭐ **置信度引导版本** - 6个任务测试 | n=6 |
| FINAL_BATCH_FIX_RESULTS.log | `根目录` | 基线版本 (无置信度引导) | n=6 |
| ARROW_NOTATION_RESULTS.log | `根目录` | 翻译任务专项测试 | n=1 |

### 📈 数据提取结果

**从日志中提取的关键数据**:

```
测试#1 - CODE_GENERATION:
  平均置信度: 0.974
  接受率: ~100.0%
  Token样本: 24

测试#2 - QA_CONVERSATION:
  平均置信度: 0.811
  接受率: ~37.3%
  Token样本: 112

测试#3 - CREATIVE_WRITING:
  平均置信度: 0.800
  接受率: ~25.0%
  Token样本: 32

测试#4 - JSON_GENERATION:
  平均置信度: 0.869
  接受率: ~81.0%
  Token样本: 69

测试#5 - MATH_REASONING:
  平均置信度: 0.789
  接受率: ~50.0%
  Token样本: 48

测试#6 - TRANSLATION:
  平均置信度: 0.841
  接受率: ~69.4%
  Token样本: 44

总计: 329个Token样本
Pearson相关系数: r ≈ 0.85-0.90 (强正相关)
```

**⚠️ 统计显著性**: 当前n=6，需扩展到n≥30以获得p<0.05

### 📋 批量测试计划

| 文档 | 位置 | 用途 |
|------|------|------|
| **BATCH_TESTING_PLAN.md** | `根目录` | 从n=6扩展到n≥30的详细计划 |
| scripts/run_batch_data_collection.sh | `scripts/` | 自动化批量测试脚本 |
| scripts/extract_all_data.py | `scripts/` | 数据提取脚本 |

---

## 3️⃣ 推测式解码基础实验

### 📄 实现文档

| 文档 | 位置 | 内容 | 论文章节 |
|------|------|------|----------|
| IMPLEMENTATION_COMPLETE.md | `根目录` | 推测式解码实现完成报告 | 第3章 方法 |
| SPECULATIVE_DECODING_COMPLETE.md | `根目录` | 完整实现总结 | 第3章 方法 |
| QUICKSTART_SPECULATIVE.md | `根目录` | 快速入门指南 | 附录 |

### 📚 技术文档

| 文档 | 位置 | 内容 | 用途 |
|------|------|------|------|
| docs/ADAPTIVE_SPECULATIVE_DECODING.md | `docs/` | 自适应推测式解码设计 | 第3章 自适应策略 |
| docs/SPECULATIVE_API_GUIDE.md | `docs/` | API使用指南 | 附录 |
| docs/TASK_AWARE_SPECULATIVE.md | `docs/` | 任务感知推测式解码 | 第3章 任务感知 |

---

## 4️⃣ 任务感知优化实验

### 📄 报告文档

| 文档 | 位置 | 内容 | 论文章节 |
|------|------|------|----------|
| **TASK_AWARE_IMPLEMENTATION_REPORT.md** | `AI-chats-linux/` | 任务感知实现报告 | 第3章 任务分类 |

### 🧪 测试程序

| 文件 | 位置 | 功能 |
|------|------|------|
| test_task_aware.cpp | `AI-chats-linux/` | 任务感知测试程序 (支持批量收集) |
| test_classifier_only.cpp | `AI-chats-linux/` | 分类器独立测试 |
| test_adaptive_speculative.cpp | `AI-chats-linux/` | 自适应推测测试 |
| test_speculative.cpp | `AI-chats-linux/` | 基础推测式解码测试 |

---

## 5️⃣ 性能优化与调试记录

### 🐛 问题诊断

| 文档 | 位置 | 问题 | 状态 |
|------|------|------|------|
| FINAL_DIAGNOSIS.md | `根目录` | 最终诊断报告 | ✅ 已解决 |
| EOS_EARLY_STOP_DIAGNOSIS.md | `AI-chats-linux/` | EOS早停问题诊断 | ✅ 已解决 |
| BATCH_SIZE_FIX_FINAL.md | `根目录` | Batch size修复 | ✅ 已修复 |
| BATCH_SIZE_FIX_STATUS.md | `根目录` | 修复状态 | ✅ 已修复 |

### 📊 性能优化

| 文档 | 位置 | 内容 |
|------|------|------|
| OPTIMIZATION_PLAN.md | `根目录` | 优化计划 |
| OPTIMIZATION_RESULTS.md | `根目录` | 优化结果 |
| OPTIMIZATION_ROADMAP.md | `根目录` | 优化路线图 |
| FINAL_OPTIMIZATION_SUMMARY.md | `根目录` | 最终优化总结 |

---

## 6️⃣ 技术设计文档

### 🏗️ 架构设计

| 文档 | 位置 | 内容 | 论文章节 |
|------|------|------|----------|
| design/speculative-decoding-architecture.md | `design/` | 推测式解码架构 | 第3章 系统架构 |
| design/kv-cache-manager-v2.md | `design/` | KV-Cache管理器设计 | 第3章 KV-Cache |

### 📖 项目文档

| 文档 | 位置 | 内容 |
|------|------|------|
| AI-chats-linux/PROJECT_SUMMARY.md | `AI-chats-linux/` | 项目总结 |
| AI-chats-linux/DOCS.md | `AI-chats-linux/` | 完整文档索引 |
| AI-chats-linux/README.md | `AI-chats-linux/` | 项目说明 |

---

## 7️⃣ 测试日志文件 (原始数据)

### 🔬 完整测试日志

| 日志文件 | 数据类型 | 测试配置 | 建议用途 |
|---------|---------|---------|---------|
| **PHASE2_CONFIDENCE_ENABLED_RESULTS.log** | ⭐ 置信度引导 | enable_confidence_guide=true | **主要数据源** |
| FINAL_BATCH_FIX_RESULTS.log | 基线对比 | 无置信度引导 | 对比实验 |
| ARROW_NOTATION_RESULTS.log | 翻译任务 | 箭头符号prompt | 任务特化 |
| CONFIDENCE_TEST_RESULTS.log | 置信度测试 | 初步测试 | 早期验证 |
| CRITICAL_FIX_test_results.log | Bug修复测试 | 修复验证 | 调试参考 |

**日志解析提示**:
- 搜索 `--- Confidence-Guided Optimization ---` 定位置信度数据
- 搜索 `Average confidence:` 提取平均置信度
- 搜索 `Accept rate:` 提取接受率
- 搜索 `Adjustments:` 提取调整次数

---

## 8️⃣ 分析脚本与工具

### 🔧 数据处理脚本

| 脚本 | 位置 | 功能 | 输出 |
|------|------|------|------|
| **scripts/extract_all_data.py** | `scripts/` | 从日志提取所有数据 | JSON + 统计摘要 |
| **scripts/analyze_confidence_correlation.py** | `scripts/` | 计算Pearson相关系数 | r, p-value, R² |
| scripts/compare_results.py | `scripts/` | 对比分析 | 对比报告 |
| scripts/run_batch_data_collection.sh | `scripts/` | 批量数据收集 | 30个数据点 |

### 📈 预期生成图表

```bash
# 运行完整分析流程
python3 scripts/extract_all_data.py          # 提取数据
python3 scripts/analyze_correlation.py        # 统计分析
python3 scripts/plot_correlation.py          # 生成图表
```

**预期输出**:
- `confidence_vs_accept_rate.png` - 散点图 + 回归线
- `confidence_by_task_type.png` - 任务类型箱线图
- `acceptance_rate_distribution.png` - 接受率分布

---

## 📝 论文撰写快速查找表

### 第1章 - 引言

**无需实验数据** - 理论背景与动机

### 第2章 - 相关工作

**参考文档**:
- `论文大纲-Phase2修正版.md` 第2章 (相关工作综述)

### 第3章 - 方法设计

| 小节 | 数据来源 | 关键文件 |
|------|---------|---------|
| 3.1 系统架构 | 设计文档 | `design/speculative-decoding-architecture.md` |
| 3.2 推测式解码 | 实现文档 | `IMPLEMENTATION_COMPLETE.md` |
| 3.3 任务感知优化 | 实现报告 | `TASK_AWARE_IMPLEMENTATION_REPORT.md` |
| **3.4 Token置信度引导** | ⭐ 核心创新 | `PHASE2_CONFIDENCE_GUIDE_COMPLETE.md` |

**关键公式与代码**:
```markdown
置信度计算: confidence = 1 - H(p) / log₂(vocab_size)
Shannon熵: H(p) = -Σ p_i * log₂(p_i)

三级阈值策略:
- 高置信度 (≥0.85): n_draft ×1.5 (aggressive)
- 中等置信度 (0.65-0.85): 保持 (moderate)
- 低置信度 (<0.65): n_draft ×0.7 (conservative)
```

### 第4章 - 实验验证

| 小节 | 数据来源 | 关键文件 |
|------|---------|---------|
| 4.1 实验设置 | 测试计划 | `BATCH_TESTING_PLAN.md` |
| **4.2 置信度与接受率相关性** | ⭐ 核心数据 | `PHASE2_DATA_SUMMARY.md` |
| 4.3 统计分析 | 分析报告 | `PHASE2_CORRELATION_ANALYSIS_REPORT.md` |
| 4.4 对比实验 | 对比报告 | `PHASE2_COMPARISON_REPORT.md` |
| 4.5 任务分类分析 | 数据汇总 | `PHASE2_DATA_SUMMARY.md` 第97-125行 |

**关键数据表格**:

```markdown
表4-1: 6种任务类型的置信度与接受率数据

| 任务类型 | 平均置信度 | 接受率 | Token样本 | 调整次数 |
|---------|-----------|--------|----------|---------|
| CODE_GENERATION | 0.974 | 100.0% | 24 | 1 |
| JSON_GENERATION | 0.869 | 81.0% | 69 | 6 |
| TRANSLATION | 0.841 | 69.4% | 44 | 6 |
| QA_CONVERSATION | 0.811 | 37.3% | 112 | 14 |
| CREATIVE_WRITING | 0.800 | 25.0% | 32 | 5 |
| MATH_REASONING | 0.789 | 50.0% | 48 | 5 |

统计摘要:
- 平均置信度: 0.847 ± 0.064
- 平均接受率: 60.5% ± 28.4%
- Pearson r ≈ 0.85-0.90 (强正相关)
- 总Token样本: 329
```

**⚠️ 重要提示**: 当前数据n=6，统计显著性不足。论文中需注明:
> "初步实验收集了6个数据点(n=6),观察到强正相关趋势(r≈0.85-0.90)。为获得统计显著性(p<0.05),需扩展样本量至n≥30。"

### 第5章 - 结果与讨论

**数据来源**:
- `PHASE2_DATA_SUMMARY.md` 第59-155行 (统计摘要与分析)
- `PHASE2_CORRELATION_ANALYSIS_REPORT.md` (详细分析)

**关键发现**:
1. 高确定性任务 (CODE, JSON): 置信度≥0.87, 接受率≥80%
2. 中等确定性任务 (QA, TRANSLATION): 置信度~0.82, 接受率~53%
3. 低确定性任务 (CREATIVE, MATH): 置信度~0.79, 接受率~38%

### 第6章 - 总结与展望

**数据来源**:
- `PHASE2_PROGRESS_SUMMARY.md` (当前进度)
- `论文大纲-Phase2修正版.md` 第6章 (未来工作)

---

## 🔍 常见查找任务

### 查找任务1: "我需要置信度与接受率的原始数据"

**位置**: `PHASE2_DATA_SUMMARY.md` 第12-20行

```
| 测试# | 任务类型 | 平均置信度 | 估算接受率 | 调整次数 | 样本数 |
```

### 查找任务2: "我需要Pearson相关系数计算"

**位置**: `PHASE2_CORRELATION_ANALYSIS_REPORT.md`

**脚本**: `scripts/analyze_confidence_correlation.py`

### 查找任务3: "我需要对比实验数据 (置信度引导 vs 基线)"

**位置**: `PHASE2_COMPARISON_REPORT.md`

**日志对比**:
- 实验组: `PHASE2_CONFIDENCE_ENABLED_RESULTS.log`
- 对照组: `FINAL_BATCH_FIX_RESULTS.log`

### 查找任务4: "我需要置信度计算的实现代码"

**位置**: `AI-chats-linux/src/inference/ConfidenceGuide.cpp` 第443行

**头文件**: `AI-chats-linux/src/inference/ConfidenceGuide.h` 第203行

**文档**: `PHASE2_CONFIDENCE_GUIDE_COMPLETE.md`

### 查找任务5: "我需要扩展数据收集到n≥30"

**计划**: `BATCH_TESTING_PLAN.md`

**脚本**: `scripts/run_batch_data_collection.sh`

**命令**:
```bash
cd /workspace
bash scripts/run_batch_data_collection.sh
```

---

## ✅ 数据完整性检查清单

### Phase 2 置信度引导实验

- [x] 实现代码 (ConfidenceGuide.h/cpp 646行)
- [x] 功能验证测试 (6个推理测试通过)
- [x] 初步数据收集 (n=6 数据点)
- [x] 数据整理与分析 (完整统计摘要)
- [x] 相关性分析 (Pearson r ≈ 0.85-0.90)
- [ ] **扩大样本量** (目标: n≥30, 当前: n=6)
- [ ] **统计显著性验证** (p<0.05)
- [ ] **数据可视化图表** (散点图 + 回归线)
- [ ] **最终实验报告**

### 推测式解码基础实验

- [x] 实现完成
- [x] API文档
- [x] 测试验证
- [x] 性能基准

### 任务感知优化实验

- [x] 任务分类器实现
- [x] 任务配置策略
- [x] 分类准确率测试
- [x] 实现报告

---

## 🚀 下一步行动 (优先级排序)

### P0 (紧急 - 论文必需)

1. **扩大样本量**: 运行 `scripts/run_batch_data_collection.sh` 获得n≥30数据
2. **统计显著性验证**: 计算精确p-value, 确保p<0.05
3. **生成可视化图表**: 散点图 + 回归线 (论文必备)

### P1 (重要 - 增强论文质量)

4. **撰写最终实验报告**: 整合所有数据
5. **性能指标收集**: Speedup, Latency, 资源效率
6. **长序列测试**: 200-500 tokens的稳定性

### P2 (可选 - 未来工作)

7. **阈值优化实验**: 测试不同阈值配置
8. **其他模型验证**: 在不同规模模型上验证

---

## 📞 技术支持

**问题**: 找不到某个数据文件?
**解决**: 使用本文档的"常见查找任务"章节

**问题**: 需要重新生成某个报告?
**解决**: 运行对应的分析脚本 (第8章)

**问题**: 数据格式不符合论文要求?
**解决**: 修改 `scripts/extract_all_data.py` 自定义输出格式

---

**文档版本**: v1.0
**最后更新**: 2025-12-02
**维护者**: Claude (AI Assistant)

**使用建议**:
1. 论文撰写前先阅读本索引
2. 使用Ctrl+F快速查找关键词
3. 优先使用标记为⭐的核心文档
4. 数据引用时注明文件来源
