# LoRA蒸馏训练使用指南

快速训练一个推测式解码专用的小Drafter模型

---

## 快速开始

### 1. 安装依赖

```bash
# 创建虚拟环境
python -m venv venv
source venv/bin/activate  # Linux/Mac
# 或 venv\Scripts\activate  # Windows

# 安装依赖
pip install torch transformers peft datasets accelerate bitsandbytes wandb
```

### 2. 最简单运行 (推荐新手)

```bash
cd scripts

# 快速测试 (10k样本，约2小时)
python train_lora_distill.py \
    --output_dir ./lora-test \
    --max_samples 10000 \
    --num_epochs 1 \
    --batch_size 2

# 训练完成后会自动保存到 ./lora-test/
```

### 3. 生产级训练 (完整版)

```bash
# 使用更多数据和更好的配置
python train_lora_distill.py \
    --output_dir ./lora-distilled-prod \
    --max_samples 50000 \
    --num_epochs 3 \
    --batch_size 4 \
    --gradient_accumulation_steps 4 \
    --lora_r 16 \
    --lora_alpha 32 \
    --temperature 2.0 \
    --alpha 0.7 \
    --use_wandb
```

---

## 参数说明

### 模型参数

```bash
--teacher_model      # 教师模型 (默认: deepseek-ai/deepseek-coder-6.7b-instruct)
--student_model      # 学生模型 (默认: TinyLlama/TinyLlama-1.1B-Chat-v1.0)
```

### LoRA参数

```bash
--lora_r 16          # LoRA秩 (越大效果越好但训练慢，推荐8-32)
--lora_alpha 32      # LoRA缩放 (通常是r的2倍)
```

### 蒸馏参数

```bash
--temperature 2.0    # 蒸馏温度 (2-4之间，越大越"软")
--alpha 0.7          # 蒸馏损失权重 (0.5-0.9，越大越依赖教师)
```

### 数据参数

```bash
--dataset            # 数据集名称
                     # 选项:
                     #   - codeparrot/github-code (默认，小而快)
                     #   - bigcode/the-stack (大而全)
                     #   - 自定义路径

--max_samples 10000  # 使用多少样本 (测试用1万，生产用5-10万)
--max_length 512     # 序列长度 (越长越好但显存占用大)
```

### 训练参数

```bash
--num_epochs 3                    # 训练轮数
--batch_size 4                    # 每GPU的batch大小
--gradient_accumulation_steps 4   # 梯度累积 (有效batch = batch_size * 这个值)
--learning_rate 5e-5              # 学习率
--warmup_steps 100                # 预热步数
```

### 其他

```bash
--use_wandb                      # 启用wandb日志记录
--wandb_project deepseek-lora    # wandb项目名
```

---

## 使用场景

### 场景1: 快速测试 (免费Colab)

**适合**: 第一次尝试，验证可行性

```bash
# 在Colab中运行
!pip install transformers peft datasets accelerate bitsandbytes

!python train_lora_distill.py \
    --output_dir /content/drive/MyDrive/lora-test \
    --max_samples 5000 \
    --num_epochs 1 \
    --batch_size 1 \
    --gradient_accumulation_steps 8
```

**预期**:
- 时间: 1-2小时
- 成本: $0 (免费)
- 效果: 提速15-20%

### 场景2: 标准训练 (本地GPU)

**适合**: 有NVIDIA GPU (12GB+ VRAM)

```bash
python train_lora_distill.py \
    --output_dir ./lora-distilled \
    --max_samples 20000 \
    --num_epochs 2 \
    --batch_size 2 \
    --gradient_accumulation_steps 4 \
    --use_wandb
```

**预期**:
- 时间: 4-6小时
- 硬件: RTX 3090 / 4090
- 效果: 提速20-25%

### 场景3: 生产级 (云端GPU)

**适合**: 追求最佳效果，有预算

```bash
# 在AWS/GCP上使用A100
python train_lora_distill.py \
    --output_dir ./lora-prod \
    --dataset bigcode/the-stack \
    --max_samples 100000 \
    --num_epochs 3 \
    --batch_size 8 \
    --gradient_accumulation_steps 2 \
    --lora_r 32 \
    --temperature 2.5 \
    --use_wandb
```

**预期**:
- 时间: 6-8小时
- 成本: $20-50
- 效果: 提速25-30%

---

## 训练后处理

### 1. 转换为GGUF

训练完成后，模型保存在 `output_dir/merged/`:

```bash
# 下载llama.cpp (如果还没有)
git clone https://github.com/ggerganov/llama.cpp.git
cd llama.cpp
make

# 转换模型
python convert_hf_to_gguf.py \
    ../scripts/lora-distilled/merged \
    --outfile tinyllama-deepseek-distilled.gguf \
    --outtype f16

# 量化
./llama-quantize \
    tinyllama-deepseek-distilled.gguf \
    tinyllama-deepseek-distilled-q4.gguf \
    Q4_K_M
```

### 2. 集成到推测式解码

```cpp
// 更新 test_speculative.cpp
const std::string drafter_path =
    "/path/to/tinyllama-deepseek-distilled-q4.gguf";
```

### 3. 性能测试

```bash
cd /path/to/AI-chats-mac/build
./test_k_optimization  # 测试不同K值
```

**预期结果**:
```
vs 原始DeepSeek-1.3B:
- Draft时间: 326ms → 200-250ms (-23-38%)
- 接受率: 65% → 50-60% (略降)
- 总延迟: 4715ms → 3500-4000ms (-15-26%)
```

---

## 常见问题

### Q1: CUDA Out of Memory 怎么办？

**解决方案**:
```bash
# 减小batch_size
--batch_size 1

# 增加梯度累积
--gradient_accumulation_steps 8

# 减小序列长度
--max_length 256

# 减小LoRA秩
--lora_r 8
```

### Q2: 训练太慢怎么办？

**解决方案**:
```bash
# 减少样本数
--max_samples 5000

# 减少轮数
--num_epochs 1

# 使用更小的数据集
--dataset codeparrot/github-code

# 减小序列长度
--max_length 256
```

### Q3: 如何评估训练效果？

**方法1: 在线监控** (训练中)
```bash
# 使用wandb
--use_wandb

# 访问 wandb.ai 查看:
# - distill_loss (应该下降)
# - ce_loss (应该下降)
# - spec_loss (应该下降)
```

**方法2: 离线评估** (训练后)
```python
# 计算接受率
from evaluate_acceptance_rate import evaluate

result = evaluate(
    teacher_model="deepseek-ai/deepseek-coder-6.7b-instruct",
    student_model="./lora-distilled/merged",
    test_prompts=["Write a Python function to...", ...]
)
print(f"Acceptance rate: {result['acceptance_rate']:.2%}")
```

### Q4: 能否进一步优化？

**可以尝试**:

1. **调整温度**:
   ```bash
   --temperature 3.0  # 更软的标签
   ```

2. **调整蒸馏权重**:
   ```bash
   --alpha 0.9  # 更依赖教师
   ```

3. **更大的LoRA**:
   ```bash
   --lora_r 32 --lora_alpha 64
   ```

4. **更多数据**:
   ```bash
   --max_samples 100000
   ```

5. **多轮训练**:
   ```bash
   --num_epochs 5
   ```

---

## 成本估算

| 配置 | 硬件 | 时间 | 成本 | 预期提速 |
|------|------|------|------|----------|
| 测试 (5k样本) | Colab免费GPU | 1-2小时 | $0 | 15-20% |
| 标准 (20k样本) | RTX 3090 | 4-6小时 | 电费 | 20-25% |
| 生产 (100k样本) | A100 (云端) | 6-8小时 | $20-50 | 25-30% |

---

## 进阶: 自定义蒸馏策略

如果想进一步优化接受率，可以修改 `train_lora_distill.py`:

### 1. 增加推测式解码专用损失权重

```python
# 在 DistillationTrainer.compute_loss() 中
loss = (
    self.alpha * distill_loss +
    (1 - self.alpha) * ce_loss +
    0.3 * spec_loss  # 增加到0.3 (默认0.1)
)
```

### 2. 使用Top-K匹配损失

```python
# 不仅优化top-1，也优化top-k
k = 5
teacher_topk = teacher_logits.topk(k, dim=-1).indices
student_probs_at_topk = torch.gather(
    student_probs,
    dim=-1,
    index=teacher_topk
)
topk_loss = -torch.log(student_probs_at_topk.sum(-1) + 1e-10).mean()

loss = ... + 0.1 * topk_loss
```

### 3. 动态温度调整

```python
# 根据训练进度调整温度
current_temp = self.temperature * (1 - self.state.epoch / self.args.num_train_epochs)
```

---

## 总结

**推荐配置** (最佳性价比):
```bash
python train_lora_distill.py \
    --output_dir ./lora-distilled \
    --max_samples 20000 \
    --num_epochs 2 \
    --batch_size 2 \
    --gradient_accumulation_steps 4 \
    --lora_r 16 \
    --temperature 2.0 \
    --alpha 0.7 \
    --use_wandb
```

**下一步**:
1. 运行训练 (4-6小时)
2. 转换为GGUF
3. 测试推测式解码性能
4. 如果效果好，考虑从头训练0.8B模型

**帮助**:
- GitHub Issues: [项目地址]
- 文档: `模型蒸馏训练方案.md`
