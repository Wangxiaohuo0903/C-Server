# LoRA蒸馏训练 - 完整总结

已为你准备好所有LoRA蒸馏训练所需的文件和脚本！

---

## 📁 已创建文件

```
scripts/
├── train_lora_distill.py      # 核心训练脚本 (12KB)
├── requirements_distill.txt    # Python依赖列表
├── quick_start.sh              # 一键启动脚本 (可执行)
├── README_LORA_DISTILL.md      # 详细使用文档
└── LORA_SUMMARY.md             # 本文件
```

---

## 🚀 三种使用方式

### 方式1: 一键启动 (最简单)

```bash
cd scripts
./quick_start.sh

# 然后按提示选择:
# 1. 快速测试 (5k样本, 1-2小时)
# 2. 标准训练 (20k样本, 4-6小时) ← 推荐
# 3. 生产级 (100k样本, 6-8小时)
```

### 方式2: 手动运行 (灵活)

```bash
cd scripts

# 1. 安装依赖
pip install -r requirements_distill.txt

# 2. 运行训练
python train_lora_distill.py \
    --output_dir ./lora-distilled \
    --max_samples 20000 \
    --num_epochs 2
```

### 方式3: 完全自定义

查看所有参数:
```bash
python train_lora_distill.py --help
```

---

## 🎯 训练流程

### 完整流程图

```
开始
  ↓
安装依赖 (pip install)
  ↓
运行训练脚本 (4-6小时)
  ├─ 加载教师模型 (DeepSeek-6.7B)
  ├─ 加载学生模型 (TinyLlama-1.1B)
  ├─ 添加LoRA适配器 (16M参数)
  ├─ 加载代码数据集 (20k样本)
  └─ 蒸馏训练 (2 epochs)
  ↓
保存模型
  ├─ LoRA权重: ./lora-distilled/
  └─ 合并模型: ./lora-distilled/merged/
  ↓
转换为GGUF (llama.cpp)
  ↓
量化为Q4 (推理加速)
  ↓
集成到推测式解码
  ↓
性能测试
  ↓
完成！预期提速20-30%
```

### 预期时间线

```
Day 1 (第1天):
09:00 - 安装环境和依赖 (30分钟)
09:30 - 启动训练 (后台运行)
15:30 - 训练完成 (6小时后)
16:00 - 转换GGUF (30分钟)
16:30 - 量化Q4 (10分钟)
17:00 - 集成测试 (30分钟)
17:30 - 完成！🎉
```

---

## 💰 成本对比

| 方案 | 硬件 | 时间 | 成本 | 提速 |
|------|------|------|------|------|
| **快速测试** | Colab免费 | 1-2h | **$0** | 15-20% |
| **标准训练** | RTX 3090 | 4-6h | 电费 | **20-25%** |
| **生产级** | A100云端 | 6-8h | $20-50 | 25-30% |

**推荐**: 先用免费Colab测试，效果好再投资标准/生产级

---

## 📊 预期效果

### 当前配置 (DeepSeek-1.3B)

```
Draft时间: 326ms
接受率: 65%
总延迟: 4715ms / 100 tokens
```

### LoRA微调后 (预期)

```
Draft时间: 200-250ms  (-23% to -38%) ✅
接受率: 50-60%        (略降)
总延迟: 3500-4000ms   (-15% to -26%) ✅
```

### ROI计算

假设每天生成1M tokens:

```
当前: 1M × 47ms = 47,000秒 = 13小时
LoRA后: 1M × 35ms = 35,000秒 = 9.7小时

节省: 3.3小时/天

如果用云GPU (p3.2xlarge $3/h):
节省: $10/天 = $300/月

训练成本回收期: $20 / $300 = 2.4天 ✅
```

**结论**: 即使是标准训练也很快回本

---

## 🔧 核心技术细节

### 损失函数

```python
# 三部分损失
loss = α * distill_loss +      # 蒸馏损失 (KL散度)
       (1-α) * ce_loss +       # 标准交叉熵
       0.1 * spec_loss         # 推测式解码专用损失

# 默认参数
α = 0.7          # 70%权重给蒸馏
temperature = 2.0  # 软标签温度
```

### LoRA配置

```python
LoraConfig(
    r=16,                    # 秩 (越大越好但慢)
    lora_alpha=32,           # 缩放因子
    target_modules=[         # 应用LoRA的层
        "q_proj", "v_proj",
        "k_proj", "o_proj"
    ],
    lora_dropout=0.05,
)

# 可训练参数: ~14M (vs 1.1B总参数)
# 训练速度: 快10倍
```

### 推测式解码专用优化

```python
# 额外损失: 鼓励学生预测和教师top-1一致
teacher_top1 = teacher_logits.argmax(-1)
student_prob_at_teacher_top1 = gather(
    student_probs,
    teacher_top1
)
spec_loss = -log(student_prob_at_teacher_top1).mean()

# 目标: 最大化接受率 (而非生成质量)
```

---

## 📖 文档索引

1. **README_LORA_DISTILL.md** - 完整使用文档
   - 参数详解
   - 场景示例
   - 常见问题
   - 进阶优化

2. **train_lora_distill.py** - 核心训练脚本
   - 自定义Trainer
   - 蒸馏损失实现
   - 推测式解码优化

3. **requirements_distill.txt** - 依赖列表

4. **quick_start.sh** - 快速启动脚本

5. **本文档 (LORA_SUMMARY.md)** - 快速总结

---

## ❓ 常见问题速查

### Q: 最低硬件要求?
A: 12GB GPU显存 (RTX 3060 / T4 / Colab免费)

### Q: 最快多久能完成?
A: 快速测试模式 1-2小时

### Q: 成本多少?
A: $0 (Colab免费) 到 $50 (生产级云端GPU)

### Q: 能提速多少?
A: 预期15-30%，具体看配置

### Q: 会降低质量吗?
A: 推测式解码只关心接受率，对最终输出质量无影响

### Q: 失败了怎么办?
A:
1. 检查CUDA/GPU驱动
2. 减小batch_size避免OOM
3. 减少样本数快速测试
4. 查看 README_LORA_DISTILL.md 的故障排除部分

---

## 🎯 快速开始 (3步)

```bash
# 1. 进入scripts目录
cd scripts

# 2. 运行快速启动脚本
./quick_start.sh

# 3. 选择 "2" (标准训练)
# 然后喝杯咖啡，等待4-6小时 ☕
```

---

## 📞 获取帮助

- **文档**: 查看 `README_LORA_DISTILL.md`
- **理论**: 查看 `../research/模型蒸馏训练方案.md`
- **代码**: 查看 `train_lora_distill.py` (有详细注释)

---

**创建时间**: 2025-10-22 21:00
**状态**: ✅ 已准备就绪，可立即使用
**下一步**: 运行 `./quick_start.sh` 开始训练！
