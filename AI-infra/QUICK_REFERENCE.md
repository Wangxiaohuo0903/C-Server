# 实验数据快速查找卡片

**💡 提示**: 这是一个快速参考指南。完整索引请查看 `EXPERIMENT_DATA_INDEX.md`

---

## 🎯 5秒速查 - 最常用文件

| 我需要... | 打开这个文件 |
|----------|-------------|
| **论文大纲** | `AI-chats-linux/论文大纲-Phase2修正版.md` |
| **置信度数据 (n=6)** | `PHASE2_DATA_SUMMARY.md` |
| **相关性分析** | `PHASE2_CORRELATION_ANALYSIS_REPORT.md` |
| **对比实验** | `PHASE2_COMPARISON_REPORT.md` |
| **批量测试计划** | `BATCH_TESTING_PLAN.md` |

---

## 📊 核心实验数据速览

### Phase 2 置信度引导 (6个数据点)

```
任务类型              置信度    接受率    样本数
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CODE_GENERATION      0.974    100.0%     24
JSON_GENERATION      0.869     81.0%     69
TRANSLATION          0.841     69.4%     44
QA_CONVERSATION      0.811     37.3%    112
CREATIVE_WRITING     0.800     25.0%     32
MATH_REASONING       0.789     50.0%     48
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
平均                 0.847     60.5%    329

Pearson r ≈ 0.85-0.90 (强正相关)
⚠️ 统计显著性: n=6不足，需n≥30
```

**数据来源**: `PHASE2_CONFIDENCE_ENABLED_RESULTS.log`

---

## 🔍 论文章节 → 数据文件映射

### 第3章 - 方法设计

```
3.1 系统架构
    → design/speculative-decoding-architecture.md

3.2 推测式解码
    → IMPLEMENTATION_COMPLETE.md

3.3 任务感知优化
    → TASK_AWARE_IMPLEMENTATION_REPORT.md

3.4 Token置信度引导 ⭐
    → PHASE2_CONFIDENCE_GUIDE_COMPLETE.md
    → AI-chats-linux/src/inference/ConfidenceGuide.cpp
```

### 第4章 - 实验验证

```
4.1 实验设置
    → BATCH_TESTING_PLAN.md (实验设计)

4.2 置信度与接受率相关性 ⭐
    → PHASE2_DATA_SUMMARY.md (第12-20行: 数据表格)
    → PHASE2_DATA_SUMMARY.md (第59-94行: 统计分析)

4.3 统计分析
    → PHASE2_CORRELATION_ANALYSIS_REPORT.md

4.4 对比实验
    → PHASE2_COMPARISON_REPORT.md

4.5 任务分类分析
    → PHASE2_DATA_SUMMARY.md (第97-125行)
```

---

## 📈 关键数据点位置

### 表格1: 置信度与接受率数据

**文件**: `PHASE2_DATA_SUMMARY.md`
**行号**: 第12-20行

### 图表1: 置信度-接受率散点图

**需生成**: 运行 `python3 scripts/plot_correlation.py`
**预期输出**: `confidence_vs_accept_rate.png`

### 公式1: Shannon熵置信度计算

**文件**: `论文大纲-Phase2修正版.md`
**章节**: 3.4.2

```
confidence = 1 - H(p) / log₂(vocab_size)
其中 H(p) = -Σ p_i * log₂(p_i)
```

### 统计量: Pearson相关系数

**当前值**: r ≈ 0.85-0.90 (初步估算, n=6)
**目标**: r 精确值 + p<0.05 (需n≥30)
**脚本**: `scripts/analyze_confidence_correlation.py`

---

## ⚡ 常见操作速查

### 查看原始测试日志

```bash
# 置信度引导版本 (实验组)
cat PHASE2_CONFIDENCE_ENABLED_RESULTS.log

# 基线版本 (对照组)
cat FINAL_BATCH_FIX_RESULTS.log
```

### 提取数据

```bash
cd /workspace
python3 scripts/extract_all_data.py
```

### 批量收集数据 (n=6 → n=30)

```bash
cd /workspace
bash scripts/run_batch_data_collection.sh
```

### 计算相关系数

```bash
python3 scripts/analyze_confidence_correlation.py
```

---

## 📁 文件分类

### 论文撰写必读 ⭐

- `论文大纲-Phase2修正版.md` - 论文结构
- `PHASE2_DATA_SUMMARY.md` - 实验数据
- `PHASE2_CORRELATION_ANALYSIS_REPORT.md` - 统计分析
- `PHASE2_COMPARISON_REPORT.md` - 对比实验

### 实现细节参考

- `PHASE2_CONFIDENCE_GUIDE_COMPLETE.md` - 实现报告
- `ConfidenceGuide.cpp` (646行) - 源代码
- `ConfidenceGuide.h` (203行) - 头文件

### 测试与验证

- `PHASE2_CONFIDENCE_ENABLED_RESULTS.log` - 主要数据源
- `FINAL_BATCH_FIX_RESULTS.log` - 基线数据
- `BATCH_TESTING_PLAN.md` - 扩展计划

---

## ✅ 当前进度

```
✅ 实现 (100%)
✅ 功能验证 (100%)
✅ 初步数据收集 (100%, n=6)
✅ 数据分析 (100%)
⏳ 扩大样本量 (20%, n=6/30)
⏳ 统计显著性 (0%, 需n≥30)
⏳ 可视化图表 (0%)
```

---

## 🚨 论文撰写注意事项

1. **数据有效性**: 当前n=6，不满足统计显著性要求
   - ✅ 可用于: 初步验证、趋势观察
   - ❌ 不可用于: 统计显著性声明

2. **正确引用方式**:
   ```
   ✅ "初步实验(n=6)显示强正相关趋势(r≈0.85-0.90)"
   ❌ "实验证明存在统计显著相关性(p<0.05)"
   ```

3. **下一步必做**:
   - 运行 `run_batch_data_collection.sh` 获得n≥30
   - 计算精确Pearson r和p-value
   - 生成论文图表

---

**完整文档**: `EXPERIMENT_DATA_INDEX.md` (600+行)
**最后更新**: 2025-12-02
