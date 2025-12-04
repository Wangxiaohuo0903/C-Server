# 任务感知推测式解码 - 测试文档

## 🎯 测试状态

**最新测试**: 2025-11-19
**状态**: ✅ 所有测试通过
**分类准确率**: 100% (13/13)

---

## 🧪 快速测试

### 运行测试程序

```bash
cd build

# 方式1: Docker
docker run --rm \
  -v "$(pwd)/..:/workspace" \
  ubuntu:22.04 bash -c "
  apt-get update -qq && apt-get install -y -qq libgomp1 && \
  cd /workspace/AI-chats-linux/build && \
  ./test_task_aware \
    --model /workspace/models/tinyllama-q4.gguf \
    --model-draft /workspace/models/tinyllama-q4.gguf"

# 方式2: 本地 (Linux/Mac)
./test_task_aware \
  --model ../models/tinyllama-q4.gguf \
  --model-draft ../models/tinyllama-q4.gguf
```

---

## 📊 测试结果摘要

### Part 1: 任务分类测试

| 任务类型 | 测试数 | 准确率 | n_draft | 温度 |
|---------|--------|-------|---------|------|
| JSON生成 | 2 | 100% | 28 | 0.1 |
| 代码生成 | 2 | 100% | 24 | 0.3 |
| 数学推理 | 2 | 100% | 22 | 0.2 |
| 翻译 | 2 | 100% | 20 | 0.5 |
| 摘要 | 1 | 100% | 18 | 0.5 |
| 问答 | 2 | 100% | 16 | 0.7 |
| 创意写作 | 2 | 100% | 8 | 1.0 |

**关键指标**:
- ✅ 分类准确率: 100%
- ✅ 平均置信度: 80%
- ✅ 中英文支持: 完美
- ✅ n_draft配置: 合理

### Part 2: 推测式解码测试

- ✅ 模型加载成功 (TinyLlama 1.1B)
- ✅ Draft模型初始化正常
- ✅ 推理功能正常
- ✅ 统计信息正确

---

## 🔬 测试用例

### 1. 代码生成 (n_draft=24)
```
用Python实现一个二分查找算法，要求有详细注释
Write a C++ function to reverse a linked list
```

### 2. JSON生成 (n_draft=28)
```
生成一个用户信息的JSON示例，包含姓名、年龄、邮箱、地址
Create a JSON config file for a web server
```

### 3. 数学推理 (n_draft=22)
```
计算 (3x + 5) * (2x - 1) 的展开式
Solve the equation: 2x^2 + 5x - 3 = 0
```

### 4. 其他任务
- 翻译: 中英互译
- 问答: 解释概念
- 创意: 诗歌、故事
- 摘要: 提取要点

---

## ⚠️ 注意事项

### 当前配置
- 使用相同模型作为target和draft（用于功能测试）
- 实际性能提升需要使用更小的draft模型

### 推荐配置
- **Target模型**: tinyllama-1.1b-q4.gguf (~638MB)
- **Draft模型**: tinyllama-160m-q4.gguf (~100MB)

---

## 🐛 故障排查

### 错误: libgomp.so.1 not found
```bash
# 安装OpenMP库
apt-get install libgomp1  # Ubuntu/Debian
yum install libgomp       # CentOS/RHEL
```

### 错误: Failed to load model
- 检查模型路径是否正确
- 确认模型文件完整未损坏

---

## 📈 性能预期

| 任务类型 | 预期加速比 | 说明 |
|---------|-----------|------|
| JSON生成 | 2.5-3.0x | 极低温度，高确定性 |
| 代码生成 | 2.2-2.8x | 低温度，高确定性 |
| 数学推理 | 2.0-2.5x | 步骤明确 |
| 翻译 | 1.8-2.2x | 中等确定性 |
| 问答 | 1.4-1.8x | 中高温度 |
| 创意写作 | 1.2-1.5x | 高温度，低确定性 |

**注**: 使用真实小draft模型时的预期值

---

## 📚 相关文档

- 实现报告: `TASK_AWARE_IMPLEMENTATION_REPORT.md`
- API文档: `../docs/TASK_AWARE_SPECULATIVE.md`
- 源代码: `src/inference/TaskClassifier.{h,cpp}`, `src/inference/SpeculativeDecoder.{h,cpp}`
