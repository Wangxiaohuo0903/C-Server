# AI-Chats-Linux 文档索引

## 📚 核心文档

### 快速开始
- **README.md** - 项目概述和基本使用
- **QUICKSTART.md** - 快速启动指南
- **QUICKREF.md** - 快速参考手册

### 开发文档
- **TASK_AWARE_IMPLEMENTATION_REPORT.md** - 任务感知推测式解码实现报告
- **TESTING.md** - 测试指南和结果（✅ 100%通过）

### 部署运维
- **DEPLOYMENT.md** - 部署指南
- **DOCKER_GUIDE.md** - Docker使用指南
- **MIGRATION_NOTES.md** - 迁移说明

### 项目管理
- **PROJECT_SUMMARY.md** - 项目总览
- **CHECKLIST.md** - 任务清单

---

## 🗂️ 文档分类

### 功能实现
```
TASK_AWARE_IMPLEMENTATION_REPORT.md
  └─ 任务感知推测式解码的完整实现文档
     - TaskClassifier设计
     - SpeculativeDecoder架构
     - 性能优化策略
```

### 测试验证
```
TESTING.md
  └─ 测试指南和结果
     - 快速测试命令
     - 测试结果摘要（100%通过）
     - 故障排查指南
```

### 使用指南
```
QUICKSTART.md      - 快速上手
QUICKREF.md        - 常用命令参考
DOCKER_GUIDE.md    - Docker容器使用
如何使用.md         - 中文使用说明
```

---

## 🎯 按需求查找

### 我想快速开始使用
→ 阅读 `QUICKSTART.md`

### 我想了解任务感知推测式解码
→ 阅读 `TASK_AWARE_IMPLEMENTATION_REPORT.md`

### 我想运行测试
→ 阅读 `TESTING.md`

### 我想部署到生产环境
→ 阅读 `DEPLOYMENT.md`

### 我想用Docker运行
→ 阅读 `DOCKER_GUIDE.md`

---

## 📂 源代码结构

```
src/inference/
├── TaskClassifier.h          - 任务分类器头文件
├── TaskClassifier.cpp        - 任务分类器实现
├── SpeculativeDecoder.h      - 推测式解码器头文件
└── SpeculativeDecoder.cpp    - 推测式解码器实现

测试程序:
├── test_task_aware.cpp       - 任务感知测试 ✅
├── test_classifier_only.cpp  - 分类器独立测试
├── test_adaptive_speculative.cpp - 自适应测试
└── test_speculative.cpp      - 基础推测式解码测试
```

---

## 🔄 最近更新

**2025-11-19**
- ✅ 修复SpeculativeDecoder编译错误
- ✅ 完成任务感知推测式解码测试（100%通过）
- ✅ 整理和精简文档结构

**2025-11-18**
- 实现TaskClassifier（7种任务类型）
- 集成任务感知优化到SpeculativeDecoder
- 编写完整测试用例

---

## 📞 问题反馈

如有问题请查看相关文档或提issue。
