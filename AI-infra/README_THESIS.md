# 🎓 毕业论文项目 - 完整资源索引

## 📁 项目文件结构

```
AI-infra/
├── AI-chats-linux/
│   ├── src/inference/
│   │   ├── SpeculativeDecoder.h        # 推测式解码器 (已完成)
│   │   ├── SpeculativeDecoder.cpp      # (已完成)
│   │   ├── TaskClassifier.h            # 任务分类器 (已完成)
│   │   ├── TaskClassifier.cpp          # (已完成)
│   │   └── ConfidenceGuide.h           # Token置信度引导 (新增 ⭐)
│   ├── test_task_aware.cpp             # 任务感知测试 (已完成)
│   └── test_confidence_guide.cpp       # 置信度测试 (新增 ⭐)
│
├── 论文规划文档/
│   ├── THESIS_PLAN.md                  # 完整论文规划 ⭐⭐⭐
│   ├── THESIS_QUICKSTART.md            # 快速启动指南 ⭐⭐
│   └── README_THESIS.md                # 本文件
│
├── 已完成成果/
│   ├── PHASE_1_1_COMPLETE.md           # Phase 1.1完成报告
│   ├── READY_TO_TEST.md                # 测试指南
│   └── ARROW_NOTATION_RESULTS.log      # 翻译优化结果
│
└── 实验数据/
    ├── FINAL_BATCH_FIX_RESULTS.log     # 完整测试结果
    └── TRANSLATION_OPTIMIZED_RESULTS.log
```

---

## 🎯 三大核心创新点总结

### 1️⃣ Token级置信度引导 (主创新点)

**技术原理**:
```
C(t) = 1 - H(p) / log2(|V|)

其中 H(p) = -Σ p_i * log2(p_i)  (Shannon熵)
```

**代码文件**: `src/inference/ConfidenceGuide.h`

**核心类**:
- `TokenConfidenceCalculator`: 计算置信度
- `ConfidenceGuidedStrategy`: 自适应调整策略

**预期贡献**:
- Accept rate提升: 15-25%
- 论文章节: 第3章 + 第6.3节实验

---

### 2️⃣ 任务感知+置信度联合优化 (协同效应)

**技术方案**:
```
最终n_draft = TaskConfig × ConfidenceMultiplier

例如:
  CODE (base=28) × 高置信度(1.5) = 42
  CREATIVE (base=8) × 低置信度(0.5) = 4
```

**已完成**: 任务感知模块 (TaskClassifier)
**待集成**: 与ConfidenceGuide联合

**预期贡献**:
- 联合优化比单独优化再提升5-10%
- 论文章节: 第4章 + 第6.4节Ablation Study

---

### 3️⃣ 边缘计算资源自适应调度 (应用价值)

**场景**:
- 低内存设备 (树莓派4B, 4GB)
- 电池供电设备 (需要省电)
- 温度管理 (防止过热)

**策略**:
```cpp
if (memory < 1GB)    → max_n_draft = 16
if (battery < 20%)   → reduce_verification
if (cpu_temp > 80°C) → reduce_n_draft(30%)
```

**预期贡献**:
- 内存占用降低30-40%
- 电池续航提升20-30%
- 论文章节: 第5章 + 第6.5节边缘设备实测

---

## 📊 当前进度 (Phase 1已完成)

### ✅ 已完成的里程碑

| Phase | 任务 | 状态 | 成果 |
|-------|------|------|------|
| 1.1 | 任务感知推测式解码 | ✅ 完成 | 分类准确率100%, Accept rate提升|
| 1.2 | 翻译任务优化 | ✅ 完成 | 13.6% → 71.1% (5.2x)|
| 2.1 | Token置信度模块 | 🔄 进行中 | 代码框架已创建 |

### 🎯 当前状态

**已有数据**:
- 6种任务类型的baseline accept rate
- 任务分类准确率: 100% (13/13)
- 翻译优化前后对比: 5.2x提升

**代码ready**:
- SpeculativeDecoder: 完整实现 ✅
- TaskClassifier: 完整实现 ✅
- ConfidenceGuide: 头文件完成 ✅
- 测试程序: 已创建 ✅

**下一步**: 集成ConfidenceGuide到SpeculativeDecoder

---

## 📝 论文写作资源

### 论文标题
**中文**: 基于Token级置信度和任务感知的边缘计算推测式解码优化研究

**英文**: Token-level Confidence and Task-aware Optimization for Speculative Decoding in Edge Computing

### 章节安排 (2-3万字)

| 章节 | 标题 | 页数 | 状态 |
|------|------|------|------|
| 1 | 绪论 | 6-8页 | ⏸️ 待开始 |
| 2 | 相关技术与理论基础 | 8-10页 | 📝 可开始写 |
| 3 | 基于Token置信度的推测优化方法 | 10-12页 | ⏸️ 需实验数据 |
| 4 | 任务感知的联合优化策略 | 8-10页 | ⏸️ 需实验数据 |
| 5 | 边缘计算场景的资源自适应调度 | 8-10页 | ⏸️ 待实现 |
| 6 | 系统实现与实验评估 | 12-15页 | 🔄 部分数据已有 |
| 7 | 总结与展望 | 3-4页 | ⏸️ 最后完成 |

**当前可开始**: 第2章 (理论基础)

---

## 🔬 实验设计清单

### 实验1: 置信度-Accept Rate相关性 ⭐⭐⭐⭐⭐
**重要性**: 核心验证实验

**步骤**:
1. 收集1000个token的置信度和accept结果
2. 计算Pearson相关系数
3. 绘制散点图和分bin统计

**预期结果**: r > 0.7 (强正相关)

**时间**: 2-3天

---

### 实验2: 阈值策略对比 ⭐⭐⭐⭐
**重要性**: 参数优化验证

**配置**:
- Conservative: high=0.90, low=0.70
- Moderate: high=0.85, low=0.65
- Aggressive: high=0.80, low=0.60

**评估**: Accept rate, Speedup, Volatility

**时间**: 2-3天

---

### 实验3: Ablation Study ⭐⭐⭐⭐⭐
**重要性**: 组件贡献度分析

**对比**:
1. Baseline (固定n_draft)
2. 仅任务感知
3. 仅置信度引导
4. 联合优化

**6种任务**: CODE, QA, CREATIVE, JSON, MATH, TRANSLATION

**时间**: 3-4天

---

### 实验4: 边缘设备测试 ⭐⭐⭐
**设备**: 树莓派4B, Jetson Nano

**测试**: 内存占用, 推理速度, 电池续航

**时间**: 2-3天

---

### 实验5: Benchmark对比 ⭐⭐⭐⭐
**对比baseline**:
- 标准推测式解码
- 自适应推测式解码
- 本文方法

**Benchmark**: MT-Bench, HumanEval, MMLU

**时间**: 3-4天

---

## ⏰ 3个月时间线

### 第1个月: 算法实现与初步实验

**Week 1** (当前):
- [x] 创建ConfidenceGuide核心代码
- [x] 创建测试程序
- [x] 制定论文规划
- [ ] 集成到SpeculativeDecoder

**Week 2**:
- [ ] 运行实验1: 置信度相关性
- [ ] 运行实验2: 阈值对比
- [ ] 撰写第2章: 理论基础

**Week 3**:
- [ ] 实现联合优化策略
- [ ] 运行实验3: Ablation Study
- [ ] 撰写第3章: 置信度方法

**Week 4**:
- [ ] 参数调优
- [ ] 数据整理
- [ ] 撰写第4章: 联合优化

---

### 第2个月: 完善实验与边缘测试

**Week 5-6**:
- [ ] 实现边缘资源调度
- [ ] 运行实验4: 边缘设备测试
- [ ] 撰写第5章: 资源调度

**Week 7-8**:
- [ ] 运行实验5: Benchmark对比
- [ ] 撰写第6章: 实验评估
- [ ] 制作所有图表

---

### 第3个月: 论文写作与完善

**Week 9-10**:
- [ ] 撰写第1章和第7章
- [ ] 撰写摘要和结论
- [ ] 第一轮完整审阅

**Week 11-12**:
- [ ] 导师审阅修改
- [ ] 格式调整
- [ ] 校对润色
- [ ] 最终定稿

---

## 📚 参考文献建议

### 核心参考 (必读)

1. **SpecDec原论文**
   - Leviathan et al. "Fast Inference from Transformers via Speculative Decoding", ICML 2023

2. **Medusa**
   - Cai et al. "Medusa: Simple Framework for Accelerating LLM Generation", arXiv 2023

3. **边缘AI综述**
   - Zhou et al. "Edge Intelligence: Paving the Last Mile of AI with Edge Computing", Proc. IEEE 2019

4. **信息论基础**
   - Shannon, C.E. "A Mathematical Theory of Communication", 1948

5. **自适应推理**
   - Schuster et al. "Confident Adaptive Language Modeling", NeurIPS 2022

### 扩展阅读 (10-15篇)

- 推测式解码相关: 3-4篇
- 大模型推理优化: 3-4篇
- 边缘计算系统: 2-3篇
- 信息论与不确定性: 2-3篇

---

## 💻 快速命令参考

### 编译测试

```bash
# 基础测试
cd AI-chats-linux/build
make test_confidence_guide
./test_confidence_guide

# 完整系统测试
make test_task_aware
./test_task_aware \
  --model ../models/tinyllama-q4.gguf \
  --model-draft ../models/tinyllama-q4.gguf
```

### 查看文档

```bash
# 查看论文规划
cat THESIS_PLAN.md

# 查看快速启动
cat THESIS_QUICKSTART.md

# 查看已完成成果
cat PHASE_1_1_COMPLETE.md
```

### 数据分析

```bash
# 分析实验结果
python scripts/analyze_confidence.py

# 生成图表
python scripts/generate_plots.py
```

---

## 📊 预期论文成果

### 定量指标

| 指标 | 目标 | 当前 |
|------|------|------|
| Accept rate提升 | +20-30% | TBD |
| 推理加速比 | 2.0-2.7x | ~2.1x (baseline) |
| 内存占用降低 | -30-40% | TBD |
| 任务分类准确率 | >95% | 100% ✅ |

### 学术贡献

1. **理论创新**: Token级置信度量化方法
2. **系统创新**: 双层联合优化框架
3. **应用创新**: 边缘计算场景适配

### 可发表内容

- **目标会议**: AAAI, ACL, NeurIPS Workshop
- **目标期刊**: 软件学报, 计算机研究与发展
- **专利**: 可申请软件著作权

---

## 🎯 质量保证

### 代码质量
- ✅ 完整注释
- ✅ 单元测试
- ✅ 性能测试
- ✅ 文档齐全

### 论文质量
- 📊 至少15个实验图表
- 📝 每个创新点独立验证
- 🔬 对比实验充分
- 📖 理论推导完整

### 数据质量
- 📈 样本量充足 (>1000)
- 📊 统计显著性 (p<0.05)
- 📉 重复实验验证
- 📋 原始数据保存

---

## 🆘 常见问题

### Q1: 实验数据不理想怎么办?
**A**:
1. 先检查代码实现是否正确
2. 调整阈值参数
3. 分析失败原因，作为讨论内容
4. 负面结果也是贡献

### Q2: 时间不够怎么办?
**A**:
1. 优先完成核心实验 (实验1, 3, 5)
2. 边缘设备测试可简化
3. Benchmark对比可选择1-2个

### Q3: 写作遇到困难?
**A**:
1. 使用提供的模板
2. 先写实验部分 (有数据支撑)
3. 理论部分可以简化
4. 多参考经典论文结构

---

## 📞 获取帮助

### 代码问题
- 查看代码注释
- 运行测试程序
- 检查编译日志

### 实验问题
- 查看THESIS_QUICKSTART.md
- 运行模拟实验验证想法
- 检查数据收集脚本

### 写作问题
- 查看THESIS_PLAN.md论文结构
- 使用提供的段落模板
- 参考现有文档格式

---

## 🎉 开始行动！

**立即开始的3件事**:

1. ✅ **阅读** `THESIS_PLAN.md` 了解完整规划
2. ✅ **编译** `test_confidence_guide` 验证代码
3. ✅ **撰写** 第2章理论基础 (可立即开始)

**本周目标**:

- [ ] ConfidenceGuide集成完成
- [ ] 第一组实验数据收集
- [ ] 第2章初稿 4-6页

---

## 📌 重要提醒

1. **每天记录进展** - 便于写实验日志
2. **保存原始数据** - 用于论文附录
3. **及时备份代码** - 使用git版本控制
4. **定期与导师沟通** - 确保方向正确

**论文成功的关键**: 持续推进 + 数据充分 + 写作清晰

---

**祝您论文顺利！有任何问题随时寻求帮助！** 🚀

---

_最后更新: 2025-11-26_
_项目状态: Phase 2启动准备中_
