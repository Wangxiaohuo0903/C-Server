# 推测式解码优化方案

## 🔍 当前问题诊断

### 问题1: 验证逻辑过于严格 ⭐⭐⭐ (最严重)

**位置:** `SpeculativeDecoder.cpp:422`

**当前代码:**
```cpp
llama_token sampled = sampleGreedy(ctx_tgt_);  // 重新采样

if (sampled == draft_token) {  // 要求完全匹配
    accepted.push_back(sampled);
} else {
    accepted.push_back(sampled);
    break;  // 拒绝并终止
}
```

**问题分析:**
- 对每个位置都**重新采样**一个新token
- 要求draft token与新采样token**完全相同**
- 即使draft token概率很高(如35%),只要不是最高(40%),就会被拒绝

**实际影响:**
```
Target模型概率分布:
  token_B: 40% (最高)
  token_A: 35% (第二)
  token_C: 25%

Draft生成: token_A
当前逻辑: 采样到token_B → 拒绝token_A ❌

正确逻辑: token_A有35%概率 → 应该接受! ✅
```

---

### 问题2: 使用了错误位置的logits ⭐⭐⭐ (严重bug)

**位置:** `SpeculativeDecoder.cpp:413 vs 202`

**代码路径:**
```cpp
// Line 413: verifyAndAccept() - 获取位置i的logits
const float* logits = llama_get_logits_ith(ctx_tgt_, i);

// Line 422: 调用sampleGreedy
llama_token sampled = sampleGreedy(ctx_tgt_);

// Line 202: sampleGreedy内部
const float* logits = llama_get_logits(ctx);  // ❌ 错误！这是最后一个位置的logits
```

**问题:**
- `llama_get_logits_ith(ctx, i)` 获取位置i的logits
- `llama_get_logits(ctx)` 获取最后一个位置的logits
- **所有draft token都用同一个(最后一个)位置的概率分布来判断!**

---

### 问题3: Q4 vs Q2 质量差距太大 ⭐⭐

**当前配置:**
- Target模型: TinyLlama-1.1B-Q4_K (4-bit量化)
- Draft模型: TinyLlama-1.1B-Q2_K (2-bit量化)

**影响:**
- Q2量化精度损失严重(~50% vs Q4的~95%)
- 两个模型输出分布差异大
- 导致draft token质量差，即使修复验证逻辑后接受率也会偏低

---

### 问题4: Draft生成使用的温度不一致 ⭐

**位置:** `SpeculativeDecoder.cpp:313`

**Draft生成:**
```cpp
llama_token next = sampleToken(ctx_dft_, config_.temperature, ...);
```

**Target验证:**
```cpp
llama_token sampled = sampleGreedy(ctx_tgt_);  // 贪婪采样 (temperature=0)
```

**问题:** Draft用`config_.temperature`(如0.7), Target用贪婪(相当于temperature=0), 不一致!

---

## 💡 优化方案

### 方案1: 修复验证逻辑 (必须) ⭐⭐⭐

#### 1.1 基于概率阈值的接受

**新增函数:** 计算指定logits的token概率
```cpp
// 在 SpeculativeDecoder.cpp 添加
float SpeculativeDecoder::getTokenProbFromLogits(
    const float* logits,
    int n_vocab,
    llama_token token
) {
    if (!logits || token < 0 || token >= n_vocab) return 0.0f;

    // 计算softmax
    float max_logit = logits[0];
    for (int i = 1; i < n_vocab; ++i) {
        max_logit = std::max(max_logit, logits[i]);
    }

    float sum_exp = 0.0f;
    for (int i = 0; i < n_vocab; ++i) {
        sum_exp += std::exp(logits[i] - max_logit);
    }

    return std::exp(logits[token] - max_logit) / sum_exp;
}
```

**修改验证逻辑:**
```cpp
// SpeculativeDecoder.cpp:410-458 替换为:

for (size_t i = 0; i < batch_size; ++i) {
    // 获取位置i的logits
    const float* logits = llama_get_logits_ith(ctx_tgt_, i);
    if (!logits) {
        if (config_.verbose) {
            std::cerr << "[Verify] No logits at position " << i << "\n";
        }
        break;
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_tgt_);
    const int n_vocab = llama_vocab_n_tokens(vocab);

    if (i == 0) {
        // 第一个位置: 采样新token
        llama_token sampled = sampleTokenFromLogits(logits, n_vocab,
                                                     temp, config_.top_k, config_.top_p);
        accepted.push_back(sampled);

        if (config_.verbose) {
            std::cerr << "[Verify] Position 0: sampled token " << sampled << "\n";
        }
    } else {
        // 验证draft token
        llama_token draft_token = draft[i - 1];

        // 计算draft token在target分布中的概率
        float draft_prob = getTokenProbFromLogits(logits, n_vocab, draft_token);

        // 概率阈值接受策略 (更宽松)
        const float PROB_THRESHOLD = 0.05f;  // 5%以上就接受

        if (draft_prob >= PROB_THRESHOLD) {
            // 接受: draft token概率足够高
            accepted.push_back(draft_token);
            if (config_.verbose) {
                std::cerr << "[Verify] ✓ Position " << i << ": accepted draft[" << (i-1)
                          << "] = " << draft_token << " (prob=" << draft_prob << ")\n";
            }
        } else {
            // 拒绝: 概率太低，采样新token
            llama_token sampled = sampleTokenFromLogits(logits, n_vocab,
                                                         temp, config_.top_k, config_.top_p);
            accepted.push_back(sampled);
            if (config_.verbose) {
                std::cerr << "[Verify] ✗ Position " << i << ": rejected draft[" << (i-1)
                          << "] = " << draft_token << " (prob=" << draft_prob
                          << "), sampled " << sampled << " instead\n";
            }
            break;
        }
    }
}
```

**预期提升:** 接受率从0-15% → **30-50%**

---

#### 1.2 Top-K接受策略 (备选)

```cpp
// 检查draft token是否在target的top-K中
bool isInTopK(const float* logits, int n_vocab, llama_token token, int K) {
    std::vector<std::pair<float, int>> scored_tokens;
    for (int i = 0; i < n_vocab; ++i) {
        scored_tokens.push_back({logits[i], i});
    }

    // 部分排序获取top-K
    std::partial_sort(scored_tokens.begin(),
                     scored_tokens.begin() + K,
                     scored_tokens.end(),
                     [](auto a, auto b) { return a.first > b.first; });

    for (int i = 0; i < K; ++i) {
        if (scored_tokens[i].second == token) return true;
    }
    return false;
}

// 验证逻辑
if (i == 0) {
    accepted.push_back(sampleTokenFromLogits(...));
} else {
    llama_token draft_token = draft[i - 1];

    if (isInTopK(logits, n_vocab, draft_token, 5)) {  // Top-5接受
        accepted.push_back(draft_token);
    } else {
        accepted.push_back(sampleTokenFromLogits(...));
        break;
    }
}
```

**优点:** 更简单，性能更好
**缺点:** K值难调

---

### 方案2: 使用更好的Draft模型 ⭐⭐

#### 2.1 改用Q4量化的Draft模型

**当前:**
```
Target: TinyLlama-Q4 (4-bit)
Draft:  TinyLlama-Q2 (2-bit)  ← 质量太差
```

**建议:**
```
Target: TinyLlama-Q4 (4-bit)
Draft:  TinyLlama-Q4 (4-bit)  ← 使用相同量化
```

**优点:**
- 质量相同，分布更接近
- 接受率显著提升 (预估50-70%)

**缺点:**
- Draft模型推理变慢
- 但整体仍有加速效果

#### 2.2 使用更小但完整的模型

**替代方案:**
```
Target: TinyLlama-1.1B-Q4
Draft:  TinyLlama-0.5B-FP16  ← 更小的完整精度模型
```

或者:
```
Target: TinyLlama-1.1B
Draft:  Phi-1.5 (小模型但质量好)
```

**预期:** 接受率40-60%, 速度2-3x

---

### 方案3: 统一采样策略 ⭐

**修改Draft生成:**
```cpp
// SpeculativeDecoder.cpp:313 - genDraft()
llama_token next = sampleToken(ctx_dft_,
                               temp,  // 使用相同温度
                               config_.top_k,
                               config_.top_p);
```

**修改Target验证:**
```cpp
// 使用相同温度采样，而非贪婪
llama_token sampled = sampleTokenFromLogits(logits, n_vocab,
                                           temp,  // 相同温度
                                           config_.top_k,
                                           config_.top_p);
```

**优点:** Draft和Target使用一致的采样策略，分布更接近

---

### 方案4: 降低p_min阈值 ⭐

**当前配置:**
```cpp
float p_min = 0.9f;  // 90%置信度才继续生成
```

**问题:** 太高，导致draft很快停止

**建议:**
```cpp
float p_min = 0.3f;  // 30%即可
```

**优点:** Draft能生成更多tokens

---

## 📊 预期效果对比

### 当前效果
```
Accept Rate:  0-15%
Speedup:      0.0-0.04x (几乎没有加速)
Draft Tokens: 2-26个/轮
```

### 优化后预期 (方案1)
```
Accept Rate:  30-50%
Speedup:      1.3-1.8x
Draft Tokens: 10-28个/轮
```

### 优化后预期 (方案1+2: 概率阈值 + Q4 Draft)
```
Accept Rate:  50-70%
Speedup:      2.0-3.0x  ← 达到理论加速比!
Draft Tokens: 15-30个/轮
```

---

## 🔧 实施优先级

### Priority 1 (必须): 修复验证逻辑
- [ ] 添加`getTokenProbFromLogits()`函数
- [ ] 修改`verifyAndAccept()`使用概率阈值 (PROB_THRESHOLD=0.05)
- [ ] 修复logits获取bug

**预期:** 接受率提升到30-50%

### Priority 2 (强烈推荐): 改善Draft模型
- [ ] 方案A: 使用TinyLlama-Q4作为draft (简单)
- [ ] 方案B: 使用更小但高质量的模型 (更优)

**预期:** 接受率提升到50-70%

### Priority 3 (建议): 调整采样参数
- [ ] 统一Draft和Target的温度
- [ ] 降低p_min到0.3
- [ ] 实现完整的temperature/top-k/top-p采样

**预期:** 额外提升5-10%

---

## 📝 测试计划

### 测试1: 验证逻辑修复后
```bash
./test_task_aware --model tinyllama-q4.gguf --model-draft tinyllama-q2-draft.gguf
```
**预期:** JSON生成接受率 15% → 35%

### 测试2: 改用Q4 Draft后
```bash
./test_task_aware --model tinyllama-q4.gguf --model-draft tinyllama-q4.gguf
```
**预期:** JSON生成接受率 35% → 60%

### 测试3: 所有优化后
```bash
# 修改代码后测试
./test_task_aware --model tinyllama-q4.gguf --model-draft tinyllama-q4.gguf
```
**预期:**
- JSON生成: 60-70%
- 代码生成: 55-65%
- 数学推理: 50-60%
- Speedup: 2.0-3.0x

---

## 🎯 结论

**核心问题:** 验证逻辑过于严格 + logits使用错误 + Draft模型质量太低

**关键修复:**
1. 使用概率阈值接受 (而非精确匹配)
2. 使用正确位置的logits
3. 使用更好的Draft模型 (Q4 instead of Q2)

**预期提升:** 0-15% → 50-70% 接受率，2-3x加速比

**下一步:** 按Priority 1 → 2 → 3 顺序实施
