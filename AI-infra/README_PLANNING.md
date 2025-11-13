# 📋 开发规划文档索引

**欢迎查看 AI-Chats 项目的开发规划！**

---

## 🎯 快速导航

根据你的需求选择合适的文档：

### 🔍 我想快速了解项目规划
**→ [PROJECT_PLAN.md](./PROJECT_PLAN.md)** ⭐推荐
- **阅读时间**: 5 分钟
- **内容**: 项目总览、时间规划、关键里程碑、核心指标
- **适合**: 快速了解全局，把握重点

---

### 📖 我想了解详细的开发路线
**→ [ROADMAP.md](./ROADMAP.md)**
- **阅读时间**: 30 分钟
- **内容**: 4 个开发阶段详细规划、技术方案、实验设计、论文撰写计划
- **适合**: 深入理解技术路线，掌握每个阶段的具体任务

---

### ✅ 我想知道本周该做什么
**→ [TASKS.md](./TASKS.md)**
- **阅读时间**: 10 分钟
- **内容**: 本周 5 天的详细任务清单，每日目标和产出
- **适合**: 立即开始工作，按日程推进
- **更新频率**: 每周更新

---

## 📚 文档对比

| 文档 | 粒度 | 时间跨度 | 更新频率 | 主要用途 |
|------|------|---------|---------|---------|
| PROJECT_PLAN.md | 宏观 | 16 周 | 按需 | 总体把握 |
| ROADMAP.md | 详细 | 16 周 | 月度 | 技术规划 |
| TASKS.md | 具体 | 1 周 | 每周 | 执行指导 |

---

## 📖 推荐阅读顺序

### 第一次阅读（30 分钟）
```
1. PROJECT_PLAN.md (5 分钟)  ← 了解全局
   ↓
2. ROADMAP.md (20 分钟)      ← 理解细节
   ↓
3. TASKS.md (5 分钟)         ← 开始行动
```

### 日常使用
```
周一早上: 打开 TASKS.md，查看本周任务
周五下午: 总结本周进度，更新 TASKS.md
月末: 回顾 ROADMAP.md，调整下月计划
```

---

## 🎯 各阶段重点文档

### 现在（Week 1-2: 核心功能修复）
**重点**: TASKS.md + ROADMAP.md 阶段 1
- 每天参考 TASKS.md 的日程安排
- 遇到问题查阅 ROADMAP.md 的技术方案

### Week 3-5（性能优化与测试）
**重点**: ROADMAP.md 阶段 2
- 参考实验设计方案
- 收集性能数据

### Week 6-9（创新功能开发）
**重点**: ROADMAP.md 阶段 3
- 分布式缓存设计
- 技术难点攻克

### Week 10-12（论文撰写）
**重点**: ROADMAP.md 阶段 4 + PROJECT_PLAN.md 指标
- 数据整理
- 论文撰写框架

---

## 📊 文档结构

```
AI-infra/
├── PROJECT_PLAN.md          # 📋 项目总览（从这里开始）
├── ROADMAP.md               # 🗺️ 详细路线图
├── TASKS.md                 # ✅ 本周任务（每周更新）
│
├── research/                # 📚 研究笔记（按周创建）
│   ├── week1-llama-api-notes.md
│   ├── week2-kv-cache-design.md
│   └── ...
│
├── design/                  # 🏗️ 设计文档
│   ├── kv-cache-manager-v2.md
│   ├── distributed-cache.md
│   └── ...
│
└── benchmark/               # 🧪 实验数据
    ├── scenarios/
    ├── results/
    └── analysis/
```

---

## 💡 使用建议

### 对于不同角色

#### 🎓 如果你是学生（自己开发）
1. 周一早上读 TASKS.md，规划本周工作
2. 遇到技术问题查 ROADMAP.md 的技术方案
3. 周五总结进度，更新下周 TASKS.md
4. 月末对照 PROJECT_PLAN.md 检查里程碑

#### 👨‍🏫 如果你是导师（指导审核）
1. 快速查看 PROJECT_PLAN.md 了解进度
2. 详细审阅 ROADMAP.md 技术方案
3. 检查 TASKS.md 任务分解是否合理

#### 👥 如果你是团队成员
1. 从 PROJECT_PLAN.md 了解项目目标
2. 在 ROADMAP.md 中找到自己负责的部分
3. 用 TASKS.md 同步工作进度

---

## 🔄 文档更新流程

### 每周（周五下午）
```bash
# 1. 总结本周进度
vim TASKS.md  # 标记完成的任务

# 2. 规划下周任务
cp TASKS.md archive/tasks-week1.md  # 归档
vim TASKS.md  # 写下周任务

# 3. 提交
git add TASKS.md
git commit -m "docs: update week 2 tasks"
```

### 每月（月末）
```bash
# 1. 回顾 ROADMAP.md
# - 检查里程碑达成情况
# - 调整后续计划

# 2. 更新 PROJECT_PLAN.md
# - 更新进度指标
# - 调整时间规划

# 3. 提交
git add ROADMAP.md PROJECT_PLAN.md
git commit -m "docs: monthly review and update"
```

---

## ✅ 检查清单

### 开始新阶段前
- [ ] 阅读 PROJECT_PLAN.md 相应章节
- [ ] 详细阅读 ROADMAP.md 该阶段内容
- [ ] 制定本周 TASKS.md
- [ ] 准备好开发环境

### 每周结束时
- [ ] 标记 TASKS.md 已完成任务
- [ ] 记录遇到的问题和解决方案
- [ ] 归档本周 TASKS.md
- [ ] 规划下周任务

### 里程碑达成时
- [ ] 对照 PROJECT_PLAN.md 检查指标
- [ ] 整理实验数据
- [ ] 更新 ROADMAP.md 进度
- [ ] 准备阶段性总结

---

## 🆘 常见问题

### Q: 计划太详细了，会不会太死板？
A: 这是一个**参考框架**，不是死规定。
- 遇到新想法随时调整
- 技术路线可以优化
- 时间安排可以灵活变化
- 关键是保持总体方向不变

### Q: 如果进度落后怎么办？
A: 及时调整计划：
1. 评估落后原因（技术难度 or 时间估算）
2. 调整 ROADMAP.md 时间规划
3. 砍掉非核心功能
4. 降低创新点难度（如分布式改为设计方案）

### Q: 如果提前完成怎么办？
A: 考虑以下方向：
1. 深化核心功能（如更多缓存策略）
2. 完善实验数据（更多场景测试）
3. 提前开始下一阶段
4. 改进代码质量和文档

### Q: 文档太多记不住怎么办？
A: 只需记住 3 个：
- **日常**: TASKS.md
- **规划**: ROADMAP.md
- **总览**: PROJECT_PLAN.md

---

## 🎯 核心信息提醒

### 时间线
- **现在**: 2025-11-10（Docker MVP 完成）
- **Week 1**: 2025-11-11 ~ 11-17（API 研究）
- **M1**: 2025-11-24（核心功能恢复）
- **M2**: 2025-12-15（性能优化完成）
- **M3**: 2026-01-12（创新点验证）
- **M4**: 2026-02-02（论文完成）

### 核心目标
- KV-Cache 缓存命中率 > 60%
- 推理延迟减少 > 30%
- 分布式缓存原型验证
- 完成毕业论文

### 本周任务（Week 1）
- Day 1: llama.cpp 源码学习
- Day 2: API 对比分析
- Day 3: Sequence 机制研究
- Day 4: 状态序列化测试
- Day 5: 设计 KVCacheManager v2

---

## 📞 需要帮助时

1. **技术问题**: 查阅 ROADMAP.md 技术方案章节
2. **进度问题**: 对照 PROJECT_PLAN.md 时间规划
3. **任务不清**: 参考 TASKS.md 详细说明
4. **找不到文档**: 回到本文档（README_PLANNING.md）

---

## 🚀 立即开始

### 现在就开始（5 分钟）
1. 阅读 PROJECT_PLAN.md 的"项目目标"和"时间规划"
2. 浏览 ROADMAP.md 的目录结构
3. 打开 TASKS.md，查看 Day 1 任务
4. 开始第一个任务！

### 建议书签
将以下文档加入浏览器书签：
- PROJECT_PLAN.md
- ROADMAP.md
- TASKS.md

---

**准备好了吗？开始你的研究之旅吧！** 🚀

**下一步**: 打开 `TASKS.md`，开始 Week 1 Day 1 的任务！
