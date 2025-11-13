# 推测式解码开发进度报告

**项目**: AI-Infra 推测式解码集成
**日期**: 2025-11-13
**当前阶段**: 设计与准备阶段完成

---

## ✅ 已完成工作

### 1. 理论研究与技术调研 ✅

**成果**:
- ✅ 深入研究推测式解码原理
- ✅ 分析 llama.cpp 的 speculative-simple 示例
- ✅ 推导理论加速比公式
- ✅ 创建详细研究笔记：`research/speculative-decoding-research.md`

**关键发现**:
- 预期加速比：1.5x - 2.5x（取决于接受率）
- Draft 模型推荐：TinyLlama-160M（速度 vs 准确性最佳平衡）
- llama.cpp 已有完整的 speculative decoding API

### 2. 系统架构设计 ✅

**成果**:
- ✅ 设计 `SpeculativeDecoder` 类接口
- ✅ 设计双模型协同工作流程
- ✅ 规划与现有 KV-Cache 优化的集成方案
- ✅ 创建架构文档：`design/speculative-decoding-architecture.md`

**核心设计**:
```
┌──────────────────────────────────────────┐
│         ModelManager                     │
│  ┌────────────────────────────────────┐ │
│  │  inferSpeculative()                │ │
│  └──────────┬─────────────────────────┘ │
│             ↓                            │
│  ┌──────────────────────────────────┐   │
│  │  SpeculativeDecoder              │   │
│  │  - Draft Model (TinyLlama-160M)  │   │
│  │  - Target Model (TinyLlama-1.1B) │   │
│  │  - genDraft()                    │   │
│  │  - verifyAndAccept()             │   │
│  └──────────────────────────────────┘   │
└──────────────────────────────────────────┘
```

### 3. Draft 模型准备 ✅

**成果**:
- ✅ 创建模型下载脚本（Bash + PowerShell）
- ✅ 编写详细的模型选择指南：`docs/draft-models-guide.md`
- ✅ 提供性能预测与对比分析

**模型推荐**:
| 模型 | 参数量 | 文件大小 | 适用场景 |
|------|--------|---------|---------|
| **TinyLlama-160M** (推荐) | 160M | ~100MB | 通用场景，最佳性价比 |
| SmolLM-135M | 135M | ~80MB | 资源受限环境 |
| TinyLlama-500M | 500M | ~300MB | 高准确性需求 |

### 4. 代码框架搭建 ✅

**成果**:
- ✅ 创建 `SpeculativeDecoder.h` 头文件
  - 完整的类定义
  - 详细的 API 文档
  - 统计与监控接口

**文件位置**:
- `AI-chats-linux/src/inference/SpeculativeDecoder.h`

---

## ⏳ 进行中工作

### 当前任务: 准备 Draft 模型

**操作步骤**:

1. **下载 Draft 模型**（Windows 环境）

   ```powershell
   # 打开 PowerShell，进入项目目录
   cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\scripts

   # 运行下载脚本
   .\download_draft_models.ps1
   # 选择 1（TinyLlama-160M）
   ```

   **预计耗时**: 5-10 分钟（取决于网速）

2. **验证下载**

   ```powershell
   # 检查文件是否存在
   ls ..\models\draft\

   # 应该看到：
   # tinyllama-160m-q4.gguf  (~100MB)
   ```

---

## 📋 待办任务

### Phase 1: 核心实现（预计 3-5 天）

- [ ] **实现 SpeculativeDecoder.cpp**
  - [ ] 构造函数（加载 draft 模型）
  - [ ] `genDraft()` 函数（draft 生成）
  - [ ] `verifyAndAccept()` 函数（验证与接受）
  - [ ] `inferTokens()` 主循环
  - [ ] 采样辅助函数
  - [ ] 统计收集

**文件**: `AI-chats-linux/src/inference/SpeculativeDecoder.cpp`

### Phase 2: 系统集成（预计 2-3 天）

- [ ] **扩展 ModelManager**
  - [ ] 添加 `inferSpeculative()` 接口
  - [ ] 添加 `loadDraftModel()` 函数
  - [ ] 添加配置管理
  - [ ] 集成到现有推理流程

**文件**: `AI-chats-linux/src/inference/ModelManager.h/cpp`

- [ ] **更新 CMakeLists.txt**
  - [ ] 添加 SpeculativeDecoder.cpp 到编译
  - [ ] 确保链接 llama.cpp

- [ ] **HTTP API 扩展**
  - [ ] `/infer` 添加 `use_speculative` 参数
  - [ ] 返回统计信息（accept_rate, speedup）

### Phase 3: 测试与优化（预计 3-5 天）

- [ ] **单元测试**
  - [ ] Draft 生成测试
  - [ ] Verify 接受测试
  - [ ] 端到端推理测试

- [ ] **性能测试**
  - [ ] 对比实验（传统 vs 推测式解码）
  - [ ] 不同 draft 模型对比
  - [ ] 不同参数（n_draft, p_min）调优

- [ ] **数据收集**
  - [ ] 接受率统计
  - [ ] 加速比测量
  - [ ] 内存占用分析

### Phase 4: 文档与论文（预计 2-3 天）

- [ ] **实验报告**
  - [ ] 性能对比图表
  - [ ] 接受率分析
  - [ ] 加速效果总结

- [ ] **更新论文**
  - [ ] 添加推测式解码章节到 `thesis-draft.md`
  - [ ] 对比 KV-Cache vs 推测式解码
  - [ ] 组合方案效果分析

---

## 📊 时间规划

```
┌────────────────────────────────────────────────────────┐
│ Week 1 (Nov 13-17): 核心实现                           │
│  ├─ Day 1-2: SpeculativeDecoder 基础实现               │
│  ├─ Day 3-4: genDraft() + verifyAndAccept()            │
│  └─ Day 5: 单元测试与调试                             │
├────────────────────────────────────────────────────────┤
│ Week 2 (Nov 18-24): 系统集成与测试                    │
│  ├─ Day 1-2: ModelManager 集成                         │
│  ├─ Day 3: HTTP API 扩展                               │
│  ├─ Day 4-5: 性能测试与数据收集                       │
│  └─ Weekend: 代码优化                                  │
├────────────────────────────────────────────────────────┤
│ Week 3 (Nov 25-Dec 1): 文档与论文                     │
│  ├─ Day 1-2: 实验报告撰写                             │
│  ├─ Day 3-4: 论文章节撰写                             │
│  └─ Day 5: 代码整理与文档完善                         │
└────────────────────────────────────────────────────────┘

预计完成时间：3 周
```

---

## 🎯 里程碑

### M1: 原型验证（Week 1 结束）
- ✅ SpeculativeDecoder 基础类实现
- ✅ 单个推理请求成功运行
- ✅ 能输出基本统计信息（accept_rate）

### M2: 系统集成（Week 2 结束）
- ✅ ModelManager 集成完成
- ✅ HTTP API 支持推测式解码
- ✅ 性能测试数据收集完成

### M3: 文档完成（Week 3 结束）
- ✅ 实验报告完成
- ✅ 论文章节完成
- ✅ 代码整理与文档完善

---

## 📚 已创建文档

| 文档 | 路径 | 用途 |
|------|------|------|
| **理论研究笔记** | `research/speculative-decoding-research.md` | 原理解析、API 分析、性能推导 |
| **架构设计文档** | `design/speculative-decoding-architecture.md` | 系统设计、算法实现、集成方案 |
| **模型选择指南** | `docs/draft-models-guide.md` | 模型对比、下载指南、性能预测 |
| **下载脚本（Bash）** | `scripts/download_draft_models.sh` | Linux/Mac 用户 |
| **下载脚本（PowerShell）** | `scripts/download_draft_models.ps1` | Windows 用户 |
| **头文件** | `AI-chats-linux/src/inference/SpeculativeDecoder.h` | 类定义、API 接口 |

---

## 🚀 立即开始

**下一步操作**（优先级排序）：

### 1. 下载 Draft 模型（必须）

```powershell
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\scripts
.\download_draft_models.ps1
# 选择 1（TinyLlama-160M）
```

### 2. 阅读关键文档（推荐）

- `research/speculative-decoding-research.md`（理解原理）
- `design/speculative-decoding-architecture.md`（理解架构）

### 3. 开始实现 SpeculativeDecoder.cpp（核心）

```cpp
// AI-chats-linux/src/inference/SpeculativeDecoder.cpp
#include "SpeculativeDecoder.h"
#include "llama.h"
#include <iostream>

// 1. 构造函数
SpeculativeDecoder::SpeculativeDecoder(
    llama_model* model_target,
    llama_context* ctx_target,
    const std::string& draft_model_path,
    const Config& config
) : model_tgt_(model_target),
    ctx_tgt_(ctx_target),
    config_(config)
{
    // TODO: 加载 draft 模型
    // TODO: 验证词汇表兼容性
}

// 2. genDraft()
std::vector<llama_token> SpeculativeDecoder::genDraft(...) {
    // TODO: 实现 draft 生成
}

// 3. verifyAndAccept()
std::vector<llama_token> SpeculativeDecoder::verifyAndAccept(...) {
    // TODO: 实现验证与接受
}

// ... 其他函数
```

---

## 📞 需要帮助？

如果遇到问题，可以：
1. 查看相关文档（见上方"已创建文档"）
2. 参考 llama.cpp 示例：`third_party/llama.cpp/examples/speculative-simple/`
3. 查看理论研究笔记中的算法伪代码

---

## 📊 当前统计

- **已完成任务**: 4/7 (57%)
- **代码行数**: ~300 行（头文件 + 文档）
- **文档页数**: ~50 页
- **预计总工作量**: 3 周

---

**加油！推测式解码是一个非常有价值的优化技术，期待看到实验结果！** 🚀
