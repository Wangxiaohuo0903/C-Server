# 🔍 推测式解码低接受率根本原因确认

## 问题现象

即使使用**相同的Q4模型**作为draft和target,接受率仍只有**3-10%**,远低于预期的50-70%。

---

## ✅ 根本原因 (已确认)

### 采样方法不一致 ⭐⭐⭐

**Draft生成:**
```cpp
// SpeculativeDecoder.cpp:370
llama_token next = sampleToken(ctx_dft_, config_.temperature, config_.top_k, config_.top_p);
```
- ✅ 使用llama.cpp原生`sampleToken`函数
- ✅ **正确实现**了temperature/top-k/top-p采样
- ✅ temperature=0.7时,能随机采样不同token

**Target验证:**
```cpp
// SpeculativeDecoder.cpp:485, 512
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, temp);
```

但 `sampleTokenFromLogits` 实现:
```cpp
// SpeculativeDecoder.cpp:307-318
} else {
    // Temperature采样 (简化版本，使用贪婪作为fallback)
    // TODO: 实现完整的temperature/top-k/top-p采样
    int best_token = 0;
    float best_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        if (logits[i] > best_logit) {
            best_logit = logits[i];
            best_token = i;
        }
    }
    return best_token;  // ❌ 仍然是贪婪采样!
}
```

### 实际情况

| 阶段 | 函数 | 实际采样策略 | 温度 |
|------|------|--------------|------|
| **Draft生成** | `sampleToken()` | ✅ 正确的temperature采样 | 0.7 |
| **Target验证** | `sampleTokenFromLogits()` | ❌ 贪婪采样 (TODO未实现) | 0.0 (实际) |

**结果:**
- Draft用temp=0.7采样,可能选择概率30%的token
- Target用greedy采样,总是拒绝非最高概率token
- 即使两个模型完全相同,策略不一致导致接受率只有3-10% ❌

---

## 💡 解决方案

### 方案A: 两边都用贪婪采样 (最简单,立即可用) ⭐⭐⭐

**修改Draft生成,使用greedy:**
```cpp
// SpeculativeDecoder.cpp:370
// 原代码:
llama_token next = sampleToken(ctx_dft_, config_.temperature, config_.top_k, config_.top_p);

// 改为:
llama_token next = sampleToken(ctx_dft_, 0.0f, 40, 0.9f);  // temperature=0.0 强制greedy
```

**修改Target验证,使用greedy:**
```cpp
// SpeculativeDecoder.cpp:485, 512
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, 0.0f);  // 已经是greedy
```

**预期效果:**
- ✅ 两个相同的Q4模型,都用greedy采样
- ✅ 相同context → 相同输出
- ✅ Accept Rate: **90-100%** (理论上几乎100%)
- ✅ Speedup: **2.5-3.5x**

**优点:**
- 代码修改最小 (只改1行)
- 立即可用,无需实现复杂采样
- 适合确定性任务 (代码生成、JSON生成)

**缺点:**
- 生成质量可能降低 (缺少随机性)
- 不适合创意写作等需要多样性的任务

---

### 方案B: 实现完整的temperature采样 (最佳,但复杂) ⭐⭐

**完整实现 `sampleTokenFromLogits`:**
```cpp
llama_token SpeculativeDecoder::sampleTokenFromLogits(
    const float* logits,
    int n_vocab,
    float temperature
) {
    if (!logits) return -1;

    // 1. 应用temperature
    std::vector<float> scaled_logits(n_vocab);
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    // 2. 计算softmax概率
    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        scaled_logits[i] = std::exp((logits[i] - max_logit) / temperature);
        sum_exp += scaled_logits[i];
    }

    // 3. 归一化
    for (int i = 0; i < n_vocab; ++i) {
        scaled_logits[i] /= sum_exp;
    }

    // 4. 随机采样
    float rand_val = (float)rand() / RAND_MAX;
    float cumsum = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        cumsum += scaled_logits[i];
        if (rand_val < cumsum) {
            return i;
        }
    }

    return n_vocab - 1;  // Fallback
}
```

**预期效果:**
- ✅ Draft和Target都用正确的temperature=0.7采样
- ✅ 相同的采样策略 → 高接受率
- ✅ Accept Rate: **50-70%**
- ✅ Speedup: **2.0-3.0x**
- ✅ 保持生成质量和多样性

**优点:**
- 适用于所有任务类型
- 保持生成质量
- 符合推测式解码原论文要求

**缺点:**
- 需要实现完整采样逻辑 (temperature + top-k + top-p)
- 代码复杂度高
- 需要处理随机数种子同步

---

### 方案C: 使用llama.cpp原生采样器 (推荐,但需要API调研) ⭐⭐⭐

**问题:** `llama_get_logits_ith(ctx, i)` 获取位置i的logits后,无法直接使用llama.cpp的采样API

**可能方案:**
1. 研究llama.cpp是否提供基于logits数组的采样函数
2. 或者重构验证逻辑,逐个位置decode并采样 (性能损失)

---

## 🧪 验证实验

### 实验1: 确认greedy采样的100%接受率

```bash
# 修改Draft生成为greedy
sed -i 's/config_.temperature/0.0f/' SpeculativeDecoder.cpp

# 重新编译测试
make test_task_aware
./test_task_aware --model tinyllama-q4.gguf --model-draft tinyllama-q4.gguf
```

**预期:** Accept Rate ≈ 100% (可能99%+ 由于浮点误差)

### 实验2: 实现完整temperature采样后测试

```bash
# 实现sampleTokenFromLogits的完整逻辑
# 重新编译测试
```

**预期:** Accept Rate ≈ 50-70%

---

## 📊 性能预测

### 当前状态 (采样不一致)
```
Draft Count:     49-157 tokens
Accept Rate:     3-10%
Speedup:         0.02-0.11x
问题:           Draft用temp=0.7, Target用greedy → 不匹配
```

### 方案A: 都用greedy
```
Draft Count:     50-200 tokens
Accept Rate:     90-100% ✅
Speedup:         2.5-3.5x ✅
适用场景:        确定性任务 (代码、JSON、翻译)
```

### 方案B: 都用temp=0.7
```
Draft Count:     50-200 tokens
Accept Rate:     50-70% ✅
Speedup:         2.0-3.0x ✅
适用场景:        所有任务 (包括创意写作)
```

---

## 🎯 立即行动计划

### Priority 1: 验证greedy方案 ⭐⭐⭐

**修改:**
```cpp
// SpeculativeDecoder.cpp:370
llama_token next = sampleToken(ctx_dft_, 0.0f, 40, 0.9f);  // 改这里!
```

**测试:**
```bash
make test_task_aware
./test_task_aware --model q4 --model-draft q4
```

**预期:** Accept Rate 90%+, Speedup 2.5x+

---

### Priority 2: 实现完整temperature采样

**修改:** `sampleTokenFromLogits`函数,实现完整温度采样逻辑

**测试:** 相同命令

**预期:** Accept Rate 50-70%, Speedup 2.0-3.0x

---

## ✅ 结论

**根本原因:** `sampleTokenFromLogits`的TODO未实现,导致采样策略不一致

**最快解决:** 两边都用greedy (改1行代码)

**最佳方案:** 实现完整temperature采样 (保证质量和加速)

**核心教训:**
1. ✅ 推测式解码要求**draft和target使用完全相同的采样策略**
2. ✅ TODO标记的代码必须完整实现,否则会导致严重性能问题
3. ✅ 验证时应该检查实际行为,而非假设代码正确

---

## 📝 下一步

1. ✅ 修改draft生成为greedy
2. ✅ 测试并确认accept rate ≈ 100%
3. ✅ 实现完整temperature采样
4. ✅ 对比greedy vs temperature的质量差异
5. ✅ 为不同任务类型选择合适的采样策略
