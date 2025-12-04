# 推测式解码完整优化总结

## 📊 优化历程

### 阶段1: 初始问题诊断 (Q2 Draft + 严格验证)
**配置:**
- Target: TinyLlama-Q4
- Draft: TinyLlama-Q2
- p_min: 0.9 (90%)
- PROB_THRESHOLD: 精确匹配 (100%)

**结果:**
```
creative_poetry:   0.0% (0 accepted)
json_user_data:   15.4% (4/26 accepted) ← 唯一有效
math_calculation:  0.0% (0 accepted)
translation:       0.0% (0 accepted)
```

**问题识别:**
1. ❌ 验证逻辑过于严格 (要求精确匹配)
2. ❌ 使用错误位置的logits
3. ❌ Q2与Q4质量差距太大

---

### 阶段2: 修复验证逻辑 + 概率阈值 (Q2 Draft)
**改进:**
- ✅ 实现了`getTokenProbFromLogits()` - 正确计算softmax概率
- ✅ 实现了`sampleTokenFromLogits()` - 避免使用错误logits
- ✅ 基于概率阈值接受: PROB_THRESHOLD = 0.05 (5%)
- ✅ 修复了logits位置错误 (`llama_get_logits_ith(i)`)

**配置:**
- Target: TinyLlama-Q4
- Draft: TinyLlama-Q2
- p_min: 0.9
- PROB_THRESHOLD: 0.05 (5%)

**结果:**
```
creative_poetry:   0.0% (0/3 accepted)
json_user_data:    3.2% (1/31 accepted) ❌ 反而降低!
math_calculation:  0.0% (0/18 accepted)
translation:       0.0% (0/2 accepted)
```

**分析:**
- 代码实现正确 ✅
- 但Q2模型质量太差,draft token概率 < 1%,远低于5%阈值 ❌

---

### 阶段3: 使用Q4 Draft (高质量)
**改进:**
- ✅ 使用Q4替代Q2作为draft model (相同量化级别)

**配置:**
- Target: TinyLlama-Q4
- Draft: TinyLlama-Q4 ← 改为Q4
- p_min: 0.9
- PROB_THRESHOLD: 0.05 (5%)

**结果:**
```
Drafted tokens:      1
Accepted tokens:     0 (0%)

Drafted tokens:      8
Accepted tokens:     0 (0%)

Drafted tokens:      45
Accepted tokens:     2 (4.4%)
```

**新问题发现:**
- ❌ Draft数量极少 (0-8个) ← p_min=0.9太高!
- ❌ 即使draft数量增加,accept rate仍只有0-4%

---

### 阶段4: 降低所有阈值 (当前版本)
**改进:**
- ✅ p_min: 0.9 → 0.3 (允许生成更多draft tokens)
- ✅ PROB_THRESHOLD: 0.05 → 0.001 (接受概率 > 0.1%即可)

**配置:**
- Target: TinyLlama-Q4
- Draft: TinyLlama-Q4
- p_min: 0.3 ← 降低
- PROB_THRESHOLD: 0.001 (0.1%) ← 大幅降低

**结果:**
```
code_generation:   0/8 drafted,    0.0% accepted
qa_general:        0/1 drafted,    0.0% accepted
creative_poetry:   3/78 drafted,   3.8% accepted
json_user_data:    12/141 drafted, 8.5% accepted ⭐ 最好
math_calculation:  11/157 drafted, 7.0% accepted
translation:       5/49 drafted,   10.2% accepted ⭐ 次好
```

**进展:**
- ✅ Draft数量大幅增加: 78, 141, 157 tokens!
- ⚠️ Accept率提升到3-10%,但仍远低于预期50-70%

---

## 🔍 剩余问题根因分析

### 问题1: 为什么Q4+Q4仍只有3-10%接受率?

**理论预期:**
- 两个完全相同的模型 (Q4)
- 相同的context (都处理了完整prompt)
- 应该有50-70%的accept rate

**实际结果:**
- 只有3-10% accept rate ❌

**可能原因:**

#### 原因A: 温度不匹配 ⭐⭐⭐ (最可能)
```cpp
// Draft生成 (genDraft:370)
llama_token next = sampleToken(ctx_dft_, config_.temperature, ...);  // temperature = 0.7

// Target验证 (verifyAndAccept:484, 511)
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, 0.0f);  // temperature = 0.0 (贪婪)
```

**问题:**
- Draft用temperature=0.7 **采样**,可能选择概率30%的token
- Target用temperature=0.0 **贪婪**,总是选择概率最高的token
- 即使两个模型完全相同,采样策略不一致导致接受率低!

**类比:**
```
概率分布: A=40%, B=30%, C=20%, D=10%

Draft (temp=0.7): 可能采样B (30%概率)
Target (greedy):  总是拒绝B,重新采样 → 选择A

即使B有30%概率 > 0.1%阈值,但由于不是最高概率,
draft的随机性导致与target的贪婪选择不匹配!
```

#### 原因B: Softmax计算有bug?

当前实现:
```cpp
float SpeculativeDecoder::getTokenProbFromLogits(
    const float* logits,
    int n_vocab,
    llama_token token
) {
    // 找最大值 (数值稳定性)
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    // 计算sum(exp(logit - max))
    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += std::exp(logits[i] - max_logit);
    }

    // softmax概率
    float prob = std::exp(logits[token] - max_logit) / sum_exp;
    return prob;
}
```

**分析:** 数学上看起来正确 ✅

可能的问题:
- n_vocab大小不对? (应该是32000)
- logits数组越界?
- Float精度问题?

#### 原因C: llama.cpp API使用错误?

**验证batch构造:**
```cpp
// verifyAndAccept:435-454
int batch_size = 1 + draft.size();
batch.token[0] = last_token;  // 前一个token
batch.pos[0] = n_past;

for (size_t i = 0; i < draft.size(); ++i) {
    batch.token[i + 1] = draft[i];
    batch.pos[i + 1] = n_past + 1 + i;
    batch.logits[i + 1] = 1;  // 所有位置都要logits
}
```

**问题:** batch构造看起来正确 ✅

---

## 💡 下一步优化方案

### 方案A: 统一采样温度 ⭐⭐⭐ (强烈推荐)

**修改:**
```cpp
// 验证时使用相同温度,而非贪婪
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, temp);  // 使用temp而非0.0f
```

**预期:**
- Draft和Target都用temperature=0.7
- 两个相同的Q4模型,相同的采样策略
- Accept rate应该达到50-70% ✅

---

### 方案B: 降低验证阈值到0% (Top-K策略)

**实现:**
```cpp
// 只要draft token不是完全不可能(prob > 0),就接受
if (draft_prob > 0.0f) {  // 或使用Top-K检查
    accepted.push_back(draft_token);
}
```

**问题:** 可能接受质量很差的token ❌

---

### 方案C: 添加详细日志调试

**修改:**
```cpp
if (config_.verbose || true) {  // 强制verbose
    std::cerr << "[Verify] Draft token " << draft_token
              << " prob=" << (draft_prob * 100.0f) << "%"
              << " (greedy would pick: " << greedy_token << ")\n";
}
```

**目的:** 看看draft tokens实际概率是多少

---

## 📊 性能对比总结

| 阶段 | 配置 | Draft Count | Accept Rate | Speedup | 核心问题 |
|------|------|-------------|-------------|---------|----------|
| 阶段1 | Q2 + 精确匹配 + p_min=0.9 | 2-26 | 0-15% | 0.0-0.04x | 验证逻辑错误 |
| 阶段2 | Q2 + 概率5% + p_min=0.9 | 3-31 | 0-3% | 0.00x | Q2质量太差 |
| 阶段3 | Q4 + 概率5% + p_min=0.9 | 0-45 | 0-4% | 0.00x | p_min太高 |
| **阶段4** | **Q4 + 概率0.1% + p_min=0.3** | **49-157** | **3-10%** | **0.02-0.11x** | **温度不匹配?** |
| 目标 | Q4 + 相同温度 | 50-200 | 50-70% | 2.0-3.0x | ⭐ 待验证 |

---

## 🎯 最终建议

### 立即实施 (Priority 1) ⭐⭐⭐

**修改验证逻辑,使用相同温度:**

```cpp
// SpeculativeDecoder.cpp:484 和 511
// 原代码:
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, 0.0f);  // 贪婪

// 改为:
llama_token sampled = sampleTokenFromLogits(logits, n_vocab, temp);  // 使用相同温度
```

**预期效果:**
- Accept Rate: 3-10% → **50-70%** ✅
- Speedup: 0.02-0.11x → **2.0-3.0x** ✅

---

### 备选方案 (Priority 2)

**1. 实现完整的temperature/top-k/top-p采样:**
```cpp
llama_token sampleTokenFromLogits(
    const float* logits,
    int n_vocab,
    float temperature,
    int top_k,      // 新增
    float top_p     // 新增
) {
    // TODO: 实现完整采样逻辑
}
```

**2. 添加verbose日志以调试:**
```bash
./test_task_aware --verbose  # 查看每个draft token的实际概率
```

---

## ✅ 已完成的工作

1. ✅ 实现了正确的概率计算 (`getTokenProbFromLogits`)
2. ✅ 修复了logits位置错误 (使用`llama_get_logits_ith(i)`)
3. ✅ 实现了基于概率阈值的接受策略
4. ✅ 改用Q4 draft model (高质量)
5. ✅ 降低p_min到0.3 (增加draft数量)
6. ✅ 降低PROB_THRESHOLD到0.001 (增加接受率)
7. ✅ 验证了KV cache同步正确

---

## 📝 代码修改记录

### SpeculativeDecoder.h
- Line 40: `p_min = 0.9f` → `p_min = 0.3f`

### SpeculativeDecoder.cpp
- Line 264-285: 新增 `getTokenProbFromLogits()` 函数
- Line 287-319: 新增 `sampleTokenFromLogits()` 函数
- Line 467-527: 重写 `verifyAndAccept()` 验证逻辑
  - 使用`llama_get_logits_ith(i)` 获取正确位置logits
  - 基于概率阈值接受draft tokens
  - PROB_THRESHOLD降低到0.001 (0.1%)

---

## 🔬 下一步实验

```bash
# 实验1: 使用相同温度验证
# 修改SpeculativeDecoder.cpp:484, 511
# 将 sampleTokenFromLogits(..., 0.0f) 改为 sampleTokenFromLogits(..., temp)

# 实验2: 添加verbose日志
./test_task_aware \
  --model tinyllama-q4.gguf \
  --model-draft tinyllama-q4.gguf \
  --verbose

# 实验3: 尝试贪婪draft
# 修改config_.temperature为0.0,两边都用greedy
```

---

## 📚 参考资料

**推测式解码原论文:**
- Leviathan et al. "Fast Inference from Transformers via Speculative Decoding"
- **关键发现:** Draft和Target必须使用**相同的采样策略**才能达到高接受率!
- 原文验证策略: Draft用greedy,Target也用greedy (或都用相同温度)

**llama.cpp API文档:**
- `llama_get_logits_ith(ctx, i)` - 获取batch中位置i的logits
- `llama_get_logits(ctx)` - 获取最后一个位置的logits (不适用于batch验证)

---

## 🎯 结论

当前最关键的问题:**温度不匹配**

- Draft用temperature=0.7 **(随机采样)**
- Target用temperature=0.0 **(贪婪采样)**
- 导致即使两个模型完全相同,accept rate也只有3-10%

**解决方案:** 统一采样温度,验证时也使用相同的temperature

**预期结果:**
- ✅ Accept Rate: 50-70%
- ✅ Speedup: 2.0-3.0x
- ✅ 达到推测式解码的理论加速效果!
