# Phase 2 批量测试数据收集计划

**创建日期**: 2025-12-02
**目标**: 将样本量从 n=6 扩展到 n≥30 以获得统计显著性

---

## 当前状态

### 已收集数据 (n=6)

| 测试# | 任务类型 | 平均置信度 | 估算接受率 | 调整次数 | 样本数 | 状态 |
|------|---------|-----------|-----------|----------|--------|------|
| 1 | CODE_GENERATION | 0.974 | ~100.0% | 1 | 24 | ✅ |
| 2 | QA_CONVERSATION | 0.811 | ~37.3% | 14 | 112 | ✅ |
| 3 | CREATIVE_WRITING | 0.800 | ~25.0% | 5 | 32 | ✅ |
| 4 | JSON_GENERATION | 0.869 | ~81.0% | 6 | 69 | ✅ |
| 5 | MATH_REASONING | 0.789 | ~50.0% | 5 | 48 | ✅ |
| 6 | TRANSLATION | 0.841 | ~69.4% | 6 | 44 | ✅ |

**初步统计**:
- 平均置信度: 0.847
- Pearson相关系数: r ≈ 0.85-0.90 (强正相关)
- **统计显著性**: ⚠️ 不显著 (n=6 太小)

---

## 数据收集目标

### 为何需要 n≥30?

**统计学原因**:
1. **中心极限定理**: n≥30 时样本分布近似正态分布
2. **p-value可靠性**: 小样本的p-value不稳定
   - n=6: p-value 可能 > 0.05 (不显著)
   - n≥30: 可以获得可靠的 p < 0.05 判断
3. **置信区间**: n≥30 可以计算有意义的95%置信区间

**论文要求**:
- 需要证明置信度与接受率的相关性是**统计显著**的
- 需要提供可靠的R² (决定系数) 估计
- 需要展示结果的可重复性

---

## 批量测试方案

### 方案A: 每种任务类型 5次重复 (推荐)

```
6 任务类型 × 5 次重复 = 30 个数据点
```

| 任务类型 | 当前数据 | 需新增 | 总计 | 特点 |
|---------|---------|--------|------|------|
| CODE_GENERATION | 1 | 4 | 5 | 结构化输出 |
| JSON_GENERATION | 1 | 4 | 5 | 格式严格 |
| QA_CONVERSATION | 1 | 4 | 5 | 多样性高 |
| CREATIVE_WRITING | 1 | 4 | 5 | 创意性强 |
| MATH_REASONING | 1 | 4 | 5 | 推理步骤 |
| TRANSLATION | 1 | 4 | 5 | 语义约束 |

**优点**:
- ✅ 每种任务覆盖均衡
- ✅ 可以分析任务内变异性
- ✅ 工作量适中 (24次新测试)

**缺点**:
- n=30 刚好达标,建议扩展到 n=36 (6×6) 更稳健

### 方案B: 每种任务类型 10次重复 (更稳健)

```
6 任务类型 × 10 次重复 = 60 个数据点
```

**优点**:
- ✅ 样本量充足,统计功效高
- ✅ 可以绘制高质量的箱线图
- ✅ 可以检测异常值

**缺点**:
- ⚠️ 工作量大 (54次新测试)
- ⚠️ 耗时长 (估计3-5天)

---

## 实验设计

### 控制变量

为确保数据可比性,所有测试必须使用**完全相同的配置**:

```cpp
// 置信度引导配置
config.enable_confidence_guide = true;
config.confidence_verbose = false;
config.high_confidence_threshold = 0.85;
config.low_confidence_threshold = 0.65;

// 模型配置 (固定)
draft_model = "tinyllama-q4.gguf"
target_model = "tinyllama-q4.gguf"

// 采样配置 (固定 greedy)
temperature = 0.0
n_draft_base = 3  // 任务感知基线
```

### 测试Prompt标准化

每种任务类型使用**固定的Prompt模板**,确保可重复性:

#### 1. CODE_GENERATION

```
Prompt: "Write a Python function to calculate the factorial of a number using recursion."
Expected length: ~100 tokens
```

#### 2. JSON_GENERATION

```
Prompt: "Generate a JSON object representing a user profile with name, age, email, and address fields."
Expected length: ~100 tokens
```

#### 3. QA_CONVERSATION

```
Prompt: "Explain what machine learning is and provide three real-world applications."
Expected length: ~150 tokens
```

#### 4. CREATIVE_WRITING

```
Prompt: "Write a short story about a robot learning to paint, in 3-4 sentences."
Expected length: ~80 tokens
```

#### 5. MATH_REASONING

```
Prompt: "If a train travels at 60 km/h for 2 hours, then 80 km/h for 1.5 hours, what is the total distance traveled? Show your work."
Expected length: ~100 tokens
```

#### 6. TRANSLATION

```
Prompt: "Translate the following sentence to French: 'The quick brown fox jumps over the lazy dog.'"
Expected length: ~50 tokens
```

---

## 数据收集流程

### 步骤1: 准备测试环境

```bash
# 在Docker容器中
cd /workspace/AI-chats-linux/build

# 确保test_task_aware已编译
make test_task_aware -j4

# 检查模型文件
ls -lh /workspace/models/tinyllama-q4.gguf
```

### 步骤2: 运行批量测试

**选项A: 自动化脚本 (推荐)**

```bash
# 运行批量收集脚本
cd /workspace
chmod +x scripts/run_batch_data_collection.sh
bash scripts/run_batch_data_collection.sh
```

**选项B: 手动逐个测试 (更可控)**

```bash
# 创建结果目录
mkdir -p /workspace/batch_results

# 示例: CODE_GENERATION 任务的5次重复测试
for i in {1..5}; do
  echo "Running CODE_GENERATION test $i/5..."
  ./test_task_aware \
    --model /workspace/models/tinyllama-q4.gguf \
    --model-draft /workspace/models/tinyllama-q4.gguf \
    2>&1 | tee /workspace/batch_results/CODE_run${i}.log
  sleep 3  # 短暂延迟
done
```

### 步骤3: 提取数据

运行数据提取脚本:

```bash
cd /workspace
python3 scripts/extract_batch_data.py
```

输出:
- `batch_data_extracted.json` - 原始数据
- `batch_statistics.txt` - 统计摘要

### 步骤4: 统计分析

```bash
python3 scripts/analyze_correlation.py
```

输出:
- Pearson correlation (r)
- p-value
- R²
- 95% 置信区间

### 步骤5: 生成可视化

```bash
python3 scripts/plot_correlation.py
```

生成图表:
- `confidence_vs_accept_rate.png` - 散点图 + 回归线
- `confidence_by_task_type.png` - 箱线图
- `acceptance_rate_distribution.png` - 直方图

---

## 数据质量检查

### 必须检查的指标

每次测试完成后,检查日志中是否包含:

```
--- Confidence-Guided Optimization ---
Average confidence:  X.XXX
Min confidence:      X.XXX
Max confidence:      X.XXX
Confidence samples:  NNN
Adjustments:         NN
```

**异常值判断**:
- 平均置信度 < 0.5 或 > 1.0 → 异常
- 置信度样本数 < 10 → 生成文本过短,可能需要重测
- 调整次数 = 0 → 可能置信度计算未生效

### 数据清洗原则

- **保留**: 所有合法的测试结果 (包括边缘案例)
- **标记**: 异常值 (如置信度=1.0的100%接受率情况)
- **删除**: 明显错误的测试 (如程序崩溃、超时)

---

## 时间估算

### 方案A (30个数据点)

```
当前已有: 6 个
需新增: 24 个

单次测试耗时: ~5-10分钟 (含模型加载和生成)
总耗时: 24 × 7分钟 = 168分钟 ≈ 3小时

建议分配:
  Day 1: CODE, JSON (8 tests) - 1 hour
  Day 2: QA, CREATIVE (8 tests) - 1 hour
  Day 3: MATH, TRANSLATION (8 tests) - 1 hour
```

### 方案B (60个数据点)

```
当前已有: 6 个
需新增: 54 个

总耗时: 54 × 7分钟 = 378分钟 ≈ 6.5小时

建议分配:
  Day 1-3: 每天 18 tests - 2 hours/day
```

---

## 数据分析计划

### 统计分析清单

一旦收集到 n≥30 的数据,执行以下分析:

#### 1. 描述性统计

- [ ] 置信度分布 (均值, 中位数, 标准差)
- [ ] 接受率分布 (均值, 中位数, 标准差)
- [ ] 各任务类型的置信度箱线图

#### 2. 相关性分析

- [ ] **Pearson相关系数** (r)
  - 预期: r ≥ 0.7 (强正相关)
- [ ] **p-value**
  - 目标: p < 0.05 (统计显著)
- [ ] **R² (决定系数)**
  - 解释: X% 的接受率变化可由置信度解释

#### 3. 回归分析

- [ ] 简单线性回归: `accept_rate = a * confidence + b`
- [ ] 计算回归系数 a, b
- [ ] 绘制回归直线和置信带

#### 4. 按任务类型分层分析

- [ ] 高确定性任务 (CODE, JSON) vs 低确定性任务 (CREATIVE, MATH)
- [ ] 不同任务类型的相关性强度对比
- [ ] ANOVA方差分析: 任务类型对置信度的影响

---

## 可视化清单

### 必须生成的图表 (论文用)

1. **置信度-接受率散点图** ⭐⭐⭐
   - X轴: 平均置信度
   - Y轴: 接受率 (%)
   - 点的颜色: 任务类型
   - 拟合线: 线性回归
   - 标注: r, p-value, R²

2. **置信度箱线图 (按任务类型)**
   - X轴: 任务类型
   - Y轴: 置信度
   - 显示中位数, 四分位数, 异常值

3. **接受率箱线图 (按任务类型)**
   - X轴: 任务类型
   - Y轴: 接受率 (%)

### 可选图表

4. **置信度分布直方图**
5. **调整次数对比柱状图**
6. **时间序列图** (如果测试跨度较长)

---

## 论文写作要点

### 实验章节结构 (建议)

```
4. 实验验证

4.1 实验设置
    - 模型配置 (TinyLlama Q4)
    - 任务类型设计 (6种任务)
    - 置信度阈值选择 (0.85/0.65)

4.2 数据收集
    - 样本量: n=30 (6任务 × 5次)
    - 测试环境 (Ubuntu 22.04, Docker)

4.3 置信度与接受率相关性分析
    - Pearson相关系数: r=X.XX (p<0.05) ✅
    - R²=X.XX (决定系数)
    - 线性回归方程

4.4 任务分类分析
    - 高确定性任务 (CODE, JSON): conf≥0.87, accept≥80%
    - 中等确定性任务 (QA, TRANSLATION): conf~0.82, accept~53%
    - 低确定性任务 (CREATIVE, MATH): conf~0.79, accept~38%

4.5 自适应策略验证
    - aggressive策略效果 (高置信度)
    - conservative策略效果 (低置信度)
```

### 关键数据点

- ✅ Pearson r (目标: ≥0.7)
- ✅ p-value (目标: <0.05)
- ✅ R² (目标: ≥0.5)
- ✅ 平均置信度: 0.84±0.06
- ✅ 平均接受率: 60±28%

---

## 下一步行动

### 优先级 P0 (本周)

- [ ] 决定使用方案A (n=30) 还是方案B (n=60)
- [ ] 编写/修改test_task_aware.cpp以支持任务类型选择
- [ ] 运行第一批测试 (CODE, JSON各5次)
- [ ] 验证数据提取脚本工作正常

### 优先级 P1 (下周)

- [ ] 完成所有30个数据点收集
- [ ] 运行统计分析脚本
- [ ] 生成所有可视化图表
- [ ] 撰写实验章节初稿

### 优先级 P2 (可选)

- [ ] 阈值优化实验 (high=0.87 vs 0.85)
- [ ] 长序列测试 (200-500 tokens)
- [ ] 对比实验 (置信度引导 vs 无引导)

---

## 相关文件

- 批量测试脚本: `scripts/run_batch_data_collection.sh`
- 数据提取脚本: `scripts/extract_batch_data.py` (待创建)
- 统计分析脚本: `scripts/analyze_correlation.py` (已有)
- 数据摘要文档: `PHASE2_DATA_SUMMARY.md`
- 对比实验报告: `PHASE2_COMPARISON_REPORT.md`

---

**文档版本**: v1.0
**最后更新**: 2025-12-02
**状态**: ⏳ 准备阶段 → 等待开始批量测试
