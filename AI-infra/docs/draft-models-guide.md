# Draft 模型选择指南

**用途**: 推测式解码（Speculative Decoding）
**更新日期**: 2025-11-13

---

## 1. 模型概览

| 模型 | 参数量 | 文件大小 | 推理速度 | 接受率 | 推荐场景 |
|------|--------|---------|---------|--------|---------|
| **SmolLM-135M** | 135M | ~80MB | 极快 (10-15ms/token) | 30-40% | 资源受限环境 |
| **TinyLlama-160M** | 160M | ~100MB | 很快 (15-20ms/token) | 40-60% | **通用推荐** |
| **TinyLlama-500M** | 500M | ~300MB | 快 (30-40ms/token) | 50-70% | 更高准确性需求 |

**Target Model**: TinyLlama-1.1B (推理速度 ~80-100ms/token)

---

## 2. 模型下载

### 2.1 Linux/Mac 用户

```bash
cd AI-infra/scripts
chmod +x download_draft_models.sh
./download_draft_models.sh
```

### 2.2 Windows 用户（手动下载）

#### 选项 1: TinyLlama-160M（推荐）

```powershell
# 创建目录
New-Item -ItemType Directory -Force -Path "..\models\draft"

# 使用 PowerShell 下载
Invoke-WebRequest -Uri "https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf" `
    -OutFile "..\models\draft\tinyllama-160m-q4.gguf"
```

或使用浏览器直接下载：
- URL: https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf
- 保存到: `AI-infra/models/draft/tinyllama-160m-q4.gguf`

#### 选项 2: SmolLM-135M（最小）

```powershell
Invoke-WebRequest -Uri "https://huggingface.co/HuggingFaceTB/SmolLM-135M-Instruct-GGUF/resolve/main/smollm-135m-instruct.Q4_K_M.gguf" `
    -OutFile "..\models\draft\smollm-135m-q4.gguf"
```

#### 选项 3: TinyLlama-500M（高质量）

```powershell
Invoke-WebRequest -Uri "https://huggingface.co/TinyLlama/TinyLlama-500M-Chat-v0.6-GGUF/resolve/main/tinyllama-500m-chat-v0.6.Q4_K_M.gguf" `
    -OutFile "..\models\draft\tinyllama-500m-q4.gguf"
```

---

## 3. 模型选择建议

### 3.1 按场景选择

#### 💻 场景 1: 普通笔记本（8GB 内存，CPU 推理）
- **推荐**: TinyLlama-160M
- **理由**:
  - 速度快（15-20ms/token）
  - 内存占用低（~100MB）
  - 接受率适中（40-60%）
  - 预期加速比：1.8-2.2x

#### 🖥️ 场景 2: 高性能工作站（16GB+ 内存，GPU 推理）
- **推荐**: TinyLlama-500M
- **理由**:
  - 更高接受率（50-70%）
  - GPU 加速下速度仍然快
  - 预期加速比：2.0-2.8x

#### 📱 场景 3: 资源受限（4-8GB 内存）
- **推荐**: SmolLM-135M
- **理由**:
  - 极小内存占用（~80MB）
  - 极快速度（10-15ms/token）
  - 虽然接受率低，但仍有加速效果
  - 预期加速比：1.5-1.8x

### 3.2 按任务类型选择

| 任务类型 | 可预测性 | 推荐模型 | 预期接受率 |
|---------|---------|---------|-----------|
| **代码生成** | 高 | TinyLlama-500M | 60-80% |
| **对话问答** | 中 | TinyLlama-160M | 40-60% |
| **创意写作** | 低 | SmolLM-135M | 30-40% |

---

## 4. 性能预测

### 4.1 理论加速比计算

```
加速比 = 1 / ((T_draft / T_target) / α + 1 / (α × N))

其中：
- T_draft: Draft 模型推理时间
- T_target: Target 模型推理时间
- α: 平均接受率
- N: Draft tokens 数量
```

#### 示例 1: TinyLlama-160M

```
T_draft = 20ms
T_target = 100ms
α = 50%
N = 16

加速比 = 1 / ((20/100)/0.5 + 1/(0.5×16))
       = 1 / (0.4 + 0.125)
       = 1 / 0.525
       ≈ 1.9x
```

#### 示例 2: TinyLlama-500M

```
T_draft = 40ms
T_target = 100ms
α = 65%
N = 16

加速比 = 1 / ((40/100)/0.65 + 1/(0.65×16))
       = 1 / (0.615 + 0.096)
       = 1 / 0.711
       ≈ 1.4x
```

**结论**: 虽然 500M 模型接受率更高，但由于推理慢，整体加速比可能不如 160M。

### 4.2 实测数据参考

基于 llama.cpp 社区报告：

| Draft Model | Target Model | Task | Accept Rate | Speedup |
|-------------|--------------|------|-------------|---------|
| TinyLlama-160M | TinyLlama-1.1B | Code Gen | 55-65% | 1.9-2.3x |
| TinyLlama-160M | TinyLlama-1.1B | Chat | 40-50% | 1.6-1.9x |
| SmolLM-135M | TinyLlama-1.1B | Code Gen | 35-45% | 1.5-1.7x |
| TinyLlama-500M | TinyLlama-1.1B | Code Gen | 60-75% | 1.7-2.0x |

---

## 5. 词汇表兼容性

### 5.1 为什么词汇表很重要？

推测式解码要求 draft 模型和 target 模型使用**相同或高度兼容**的词汇表，因为：
1. Draft tokens 直接作为 target 模型的输入
2. 不兼容的词汇表会导致验证失败

### 5.2 兼容性检查

llama.cpp 会自动检查：
- 词汇表类型（BPE/SentencePiece）
- 特殊 tokens（BOS/EOS/PAD）
- 前 N 个 token 的内容一致性

**TinyLlama 系列**：
- ✅ TinyLlama-160M 与 TinyLlama-1.1B **完全兼容**
- ✅ TinyLlama-500M 与 TinyLlama-1.1B **完全兼容**

**SmolLM 系列**：
- ⚠️ SmolLM-135M 与 TinyLlama-1.1B **可能不兼容**
- 需要实际测试验证

### 5.3 验证词汇表

```cpp
// llama.cpp 会自动验证
if (!common_speculative_are_compatible(ctx_tgt, ctx_dft)) {
    LOG_INF("Draft model is not compatible with target model\n");
}
```

---

## 6. 快速测试

### 6.1 测试脚本

下载模型后，运行快速测试：

```bash
# 进入项目目录
cd AI-infra/AI-chats-linux

# 编译项目（如果尚未编译）
mkdir -p build && cd build
cmake .. && make -j4
cd ..

# 测试推测式解码
./build/ai_infra_server_mac \
    --model ../models/tinyllama-1.1b-q4.gguf \
    --model-draft ../models/draft/tinyllama-160m-q4.gguf \
    --prompt "用Python实现快速排序" \
    --n-predict 100 \
    --draft-max 16
```

### 6.2 预期输出

```
[Draft] Generated 16 tokens in 320ms
[Verify] Accepted 9/16 tokens (56.25%)
[Stats] Speedup: 1.95x

generated text:
```python
def quick_sort(arr):
    if len(arr) <= 1:
        return arr
    ...
```
```

---

## 7. 故障排除

### 问题 1: 下载速度慢

**解决方案**:
```bash
# 使用镜像加速（中国大陆用户）
export HF_ENDPOINT=https://hf-mirror.com

# 或使用代理
export https_proxy=http://127.0.0.1:7890
```

### 问题 2: 词汇表不兼容

**症状**:
```
LOG_INF: Draft model is not compatible with target model
```

**解决方案**:
- 使用同一家族的模型（如 TinyLlama 系列）
- 检查模型训练时使用的 tokenizer

### 问题 3: 接受率过低（<20%）

**可能原因**:
- Draft 模型质量差
- Draft tokens 数量过多（N=16 → N=8）
- 置信度阈值过低（p_min=0.9 → 0.95）

**解决方案**:
```cpp
// 调整配置
config.n_draft = 8;       // 减少 draft 数量
config.p_min = 0.95f;     // 提高置信度阈值
```

### 问题 4: 内存不足

**症状**:
```
ERROR: Failed to allocate memory for draft model
```

**解决方案**:
- 使用更小的模型（SmolLM-135M）
- 减少上下文长度
- 使用 CPU-only 模式（不加载到 GPU）

---

## 8. 下一步

1. ✅ 下载 draft 模型
2. ⏳ 实现 `SpeculativeDecoder` 类
3. ⏳ 集成到 `ModelManager`
4. ⏳ 运行性能测试
5. ⏳ 收集实验数据

---

## 9. 参考资源

### 9.1 Hugging Face 模型页面

- **TinyLlama-160M**: https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF
- **TinyLlama-500M**: https://huggingface.co/TinyLlama/TinyLlama-500M-Chat-v0.6-GGUF
- **SmolLM-135M**: https://huggingface.co/HuggingFaceTB/SmolLM-135M-Instruct-GGUF

### 9.2 llama.cpp 文档

- **Speculative Decoding**: https://github.com/ggerganov/llama.cpp/tree/master/examples/speculative
- **API 参考**: `llama.cpp/llama.h`

### 9.3 论文

- **Fast Inference from Transformers via Speculative Decoding** (Chen et al., 2023)
  https://arxiv.org/abs/2211.17192

---

**最后更新**: 2025-11-13
**维护者**: xiaohuo
