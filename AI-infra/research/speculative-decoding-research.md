# 推测式解码（Speculative Decoding）研究笔记

**日期**: 2025-11-13
**作者**: xiaohuo
**基于**: llama.cpp speculative-simple 示例

---

## 1. 核心原理

### 1.1 什么是推测式解码？

推测式解码是一种加速自回归语言模型推理的技术，核心思想：

```
传统自回归（串行）：
Token1 → Token2 → Token3 → Token4 → ...
  50ms    50ms     50ms     50ms      (每个token需要一次完整的模型推理)

推测式解码（并行验证）：
小模型快速生成草稿：Token1, Token2, Token3, Token4 (10ms)
                         ↓
大模型一次性验证：   ✓    ✓    ✓    ✗     (50ms)
接受前3个，拒绝第4个并修正

时间对比：
- 传统方式生成3个token：50ms × 3 = 150ms
- 推测式解码生成3个token：10ms + 50ms = 60ms
- 加速比：150ms / 60ms = 2.5x
```

### 1.2 为什么有效？

**关键洞察**：
1. **小模型推理速度快**：参数量少（如 160M vs 1.1B），推理延迟低（~10ms vs ~50ms）
2. **大模型并行验证高效**：验证 N 个 tokens 的时间 ≈ 生成 1 个 token 的时间（batch parallelism）
3. **Draft 接受率适中**：小模型通常能正确预测 30-70% 的 tokens

**ROI 计算**：
```
设：
- 大模型推理延迟：T_target = 50ms
- 小模型推理延迟：T_draft = 10ms
- Draft tokens 数量：N = 16
- Draft 接受率：α = 50%

传统方式生成 α*N 个 tokens 的时间：
  Time_normal = α * N * T_target = 0.5 × 16 × 50ms = 400ms

推测式解码生成 α*N 个 tokens 的时间：
  Time_spec = T_draft + T_target = 10ms + 50ms = 60ms

加速比 = 400ms / 60ms ≈ 6.7x
```

---

## 2. llama.cpp 实现分析

### 2.1 核心数据结构

```cpp
// 目标模型上下文
llama_context * ctx_tgt = NULL;  // 大模型（如 TinyLlama-1.1B）

// 草稿模型上下文
llama_context * ctx_dft = NULL;  // 小模型（如 TinyLlama-160M）

// Speculative 状态管理
struct common_speculative * spec = common_speculative_init(ctx_tgt, ctx_dft);
```

### 2.2 推理流程（从 speculative-simple.cpp）

#### Step 1: 初始化

```cpp
// 加载目标模型
common_init_result llama_init_tgt = common_init_from_params(params);
model_tgt = llama_init_tgt.model.get();
ctx_tgt   = llama_init_tgt.context.get();

// 加载草稿模型
params.model = params.speculative.model;
common_init_result llama_init_dft = common_init_from_params(params);
ctx_dft   = llama_init_dft.context.get();

// 验证词汇表兼容性（重要！）
if (!common_speculative_are_compatible(ctx_tgt, ctx_dft)) {
    LOG_INF("draft model is not compatible with target model\n");
}
```

#### Step 2: 处理 Prompt

```cpp
// Tokenize prompt
std::vector<llama_token> inp = common_tokenize(ctx_tgt, params.prompt, true, true);

// Target model 推理 prompt（除最后一个token）
llama_decode(ctx_tgt, llama_batch_get_one(inp.data(), inp.size() - 1));

llama_token id_last = inp.back();  // 保存最后一个token
llama_tokens prompt_tgt(inp.begin(), inp.end() - 1);
int n_past = inp.size() - 1;
```

#### Step 3: 主循环（Draft → Verify → Accept）

```cpp
while (true) {
    // ========== (1) Draft 生成 ==========
    llama_tokens draft = common_speculative_gen_draft(
        spec, params_spec, prompt_tgt, id_last
    );
    // draft.size() 通常为 N (如 16 个 tokens)

    // ========== (2) 构造 Batch ==========
    common_batch_clear(batch_tgt);
    common_batch_add(batch_tgt, id_last, n_past++, { 0 }, true);

    // 添加所有 draft tokens 到 batch
    for (size_t i = 0; i < draft.size(); ++i) {
        common_batch_add(batch_tgt, draft[i], n_past + i, { 0 }, true);
    }

    // ========== (3) 大模型验证 ==========
    llama_decode(ctx_tgt, batch_tgt);
    // 一次性推理：[id_last, draft[0], draft[1], ..., draft[N-1]]

    // ========== (4) 采样与接受 ==========
    const auto ids = common_sampler_sample_and_accept_n(smpl, ctx_tgt, draft);
    // ids 包含所有被接受的 tokens（至少包含 id_last）

    // ========== (5) 更新状态 ==========
    n_past    += ids.size() - 1;
    n_drafted += draft.size();
    n_accept  += ids.size() - 1;

    for (size_t i = 0; i < ids.size(); ++i) {
        prompt_tgt.push_back(id_last);
        id_last = ids[i];
        LOG("%s", common_token_to_piece(ctx_tgt, id_last).c_str());
    }

    // ========== (6) 清理 KV Cache ==========
    // 删除未接受的 draft tokens 的 KV cache
    llama_memory_seq_rm(llama_get_memory(ctx_tgt), 0, n_past, -1);

    if (has_eos || n_predict > max_predict) break;
}
```

### 2.3 关键函数

#### `common_speculative_gen_draft()`

**功能**：使用草稿模型生成 N 个候选 tokens

**伪代码**：
```cpp
llama_tokens common_speculative_gen_draft(
    common_speculative * spec,
    common_speculative_params & params,
    const llama_tokens & prompt_tgt,
    llama_token id_last
) {
    llama_tokens draft;

    // 草稿模型生成 N 个 tokens
    for (int i = 0; i < params.n_draft; ++i) {
        // 单步推理
        llama_decode(ctx_dft, ...);

        // 采样
        llama_token next = sample_greedy(ctx_dft);

        // 置信度检查（可选）
        if (confidence(next) < params.p_min) {
            break;  // 停止 draft
        }

        draft.push_back(next);
    }

    return draft;
}
```

#### `common_sampler_sample_and_accept_n()`

**功能**：从大模型的 logits 中采样，并逐个验证 draft tokens

**伪代码**：
```cpp
llama_tokens common_sampler_sample_and_accept_n(
    common_sampler * smpl,
    llama_context * ctx_tgt,
    const llama_tokens & draft
) {
    llama_tokens accepted;

    // 对每个位置进行采样
    for (size_t i = 0; i < draft.size() + 1; ++i) {
        // 获取当前位置的 logits
        const float* logits = llama_get_logits_ith(ctx_tgt, i);

        // 采样
        llama_token sampled = sample(smpl, logits);

        if (i == 0) {
            // 第一个 token（id_last 的下一个）总是接受
            accepted.push_back(sampled);
        } else {
            // 验证 draft token
            if (sampled == draft[i - 1]) {
                // 接受
                accepted.push_back(sampled);
            } else {
                // 拒绝，使用采样的 token 并终止
                accepted.push_back(sampled);
                break;
            }
        }
    }

    return accepted;
}
```

---

## 3. 性能分析

### 3.1 影响因素

| 因素 | 说明 | 影响 |
|------|------|------|
| **Draft 长度 N** | 每次生成的 draft tokens 数量 | N 越大，潜在加速越高，但接受率可能降低 |
| **接受率 α** | 被大模型接受的 draft tokens 比例 | α 越高，加速效果越好 |
| **T_draft / T_target** | 小模型与大模型的速度比 | 比值越小，draft 开销越低 |
| **模型相似度** | Draft 模型与 Target 模型的相似性 | 越相似，接受率越高 |

### 3.2 理想加速比推导

**符号定义**：
- `N`：draft tokens 数量
- `α`：平均接受率
- `T_d`：draft 模型推理一个 token 的时间
- `T_t`：target 模型推理一个 token 的时间

**传统自回归生成 M 个 tokens**：
```
Time_normal = M × T_t
```

**推测式解码生成 M 个 tokens**：

每轮推测式解码：
- Draft 阶段：生成 N 个 tokens，耗时 `N × T_d`
- Verify 阶段：验证 N 个 tokens，耗时 `T_t`（batch parallelism）
- 接受数量：`α × N` 个 tokens

平均生成 1 个 token 的时间：
```
Time_per_token = (N × T_d + T_t) / (α × N)
                = T_d / α + T_t / (α × N)
```

生成 M 个 tokens 的总时间：
```
Time_spec = M × Time_per_token
          = M × (T_d / α + T_t / (α × N))
```

**加速比**：
```
Speedup = Time_normal / Time_spec
        = (M × T_t) / (M × (T_d / α + T_t / (α × N)))
        = T_t / (T_d / α + T_t / (α × N))
        = 1 / (T_d / (α × T_t) + 1 / (α × N))
```

**特殊情况分析**：

1. **如果 T_d << T_t**（draft 很快），则：
   ```
   Speedup ≈ 1 / (1 / (α × N)) = α × N
   ```
   理论最大加速：`α × N`

2. **如果 α = 100%**（draft 完全正确）：
   ```
   Speedup = 1 / (T_d / T_t + 1 / N)
   ```

3. **实际情况**（T_d = 0.2 × T_t, α = 50%, N = 16）：
   ```
   Speedup = 1 / (0.2 / 0.5 + 1 / (0.5 × 16))
           = 1 / (0.4 + 0.125)
           = 1 / 0.525
           ≈ 1.9x
   ```

### 3.3 llama.cpp 实验数据

从 llama.cpp 文档和社区报告：

| Target Model | Draft Model | Draft N | Accept Rate | Speedup |
|--------------|-------------|---------|-------------|---------|
| LLaMA-7B | LLaMA-160M | 16 | 40-60% | 1.5-2.0x |
| LLaMA-13B | LLaMA-1B | 16 | 50-70% | 2.0-2.5x |
| Qwen2.5-32B | Qwen2.5-1.5B | 16 | 60-80% | 2.5-3.0x |

**结论**：实际加速比通常在 **1.5x - 3.0x** 之间。

---

## 4. 实现计划

### 4.1 架构设计

```
┌──────────────────────────────────────────────────────┐
│                 ModelManager                         │
│  ┌────────────────────────────────────────────────┐ │
│  │  inferSpeculative(chat_id, user_msg, ...)     │ │
│  └────────────────────────────────────────────────┘ │
│                        ↓                             │
│  ┌────────────────────────────────────────────────┐ │
│  │     SpeculativeDecoder                         │ │
│  │  ┌──────────────┐       ┌──────────────┐      │ │
│  │  │ Draft Model  │       │ Target Model │      │ │
│  │  │ (TinyLlama   │       │ (TinyLlama   │      │ │
│  │  │  160M/500M)  │       │  1.1B)       │      │ │
│  │  └──────────────┘       └──────────────┘      │ │
│  │         ↓                      ↓               │ │
│  │    genDraft()           verifyAndAccept()      │ │
│  └────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
```

### 4.2 新增类设计

#### `SpeculativeDecoder.h`

```cpp
#pragma once

#include "llama.h"
#include <string>
#include <vector>

class SpeculativeDecoder {
public:
    struct Config {
        int n_draft = 16;           // Draft tokens 数量
        int n_draft_min = 5;        // 最小 draft 数量（低于此值跳过）
        float p_min = 0.9f;         // Draft 置信度阈值
        int n_threads_draft = 2;    // Draft 模型线程数
    };

    // 构造函数
    SpeculativeDecoder(
        llama_model* model_target,
        llama_context* ctx_target,
        const std::string& draft_model_path,
        const Config& config
    );

    ~SpeculativeDecoder();

    // 推测式推理
    std::string infer(
        const std::string& prompt,
        int max_tokens,
        float temperature
    );

    // 获取统计信息
    struct Stats {
        uint64_t n_drafted = 0;
        uint64_t n_accepted = 0;
        double accept_rate = 0.0;
        double speedup = 0.0;
    };
    Stats getStats() const;

private:
    llama_model* model_tgt_;
    llama_context* ctx_tgt_;

    llama_model* model_dft_;
    llama_context* ctx_dft_;

    Config config_;
    Stats stats_;

    // Draft 生成
    std::vector<llama_token> genDraft(
        const std::vector<llama_token>& prompt,
        llama_token last_token
    );

    // 验证并接受
    std::vector<llama_token> verifyAndAccept(
        const std::vector<llama_token>& draft,
        llama_token last_token,
        int n_past
    );
};
```

### 4.3 集成到 ModelManager

修改 `ModelManager.h`：

```cpp
class ModelManager {
public:
    // 现有接口（KV-Cache 优化）
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens,
                      float temperature);

    // 新增：推测式解码接口
    std::string inferSpeculative(const std::string& chat_id,
                                  const std::string& user_msg,
                                  int maxTokens,
                                  float temperature);

    // 加载 draft 模型
    bool loadDraftModel(const std::string& path);

private:
    std::unique_ptr<SpeculativeDecoder> spec_decoder_;
};
```

### 4.4 开发步骤

**Phase 1: 原型验证（3-5天）**
- [ ] 实现 `SpeculativeDecoder` 基础类
- [ ] 实现 `genDraft()` 函数
- [ ] 实现 `verifyAndAccept()` 函数
- [ ] 单元测试（独立运行）

**Phase 2: 集成到系统（2-3天）**
- [ ] 修改 `ModelManager` 添加 `inferSpeculative()` 接口
- [ ] 添加 draft 模型加载逻辑
- [ ] 添加配置选项（draft model path, n_draft, p_min）
- [ ] HTTP API 支持（`/infer` 添加 `use_speculative` 参数）

**Phase 3: 性能测试（2-3天）**
- [ ] 实现对比测试框架
- [ ] 测试不同 draft 模型的效果
- [ ] 测试不同 N、p_min 参数的影响
- [ ] 收集数据（accept rate, speedup）

**Phase 4: 文档与论文（3-5天）**
- [ ] 更新实验结果到 thesis-draft.md
- [ ] 编写部署文档
- [ ] 制作对比图表

---

## 5. Draft 模型选择

### 5.1 候选模型

| 模型 | 参数量 | 适用场景 |
|------|--------|---------|
| **TinyLlama-160M** | 160M | 极致速度，适合 CPU 推理 |
| **TinyLlama-500M** | 500M | 平衡速度与准确性 |
| **Qwen2.5-0.5B** | 500M | 中文场景更优 |
| **SmolLM-135M** | 135M | 超小模型，极低延迟 |

### 5.2 下载地址

```bash
# TinyLlama-160M (推荐)
wget https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf

# TinyLlama-500M
wget https://huggingface.co/TinyLlama/TinyLlama-500M-Chat-v0.6-GGUF/resolve/main/tinyllama-500m-chat-v0.6.Q4_K_M.gguf

# SmolLM-135M
wget https://huggingface.co/HuggingFaceTB/SmolLM-135M-Instruct-GGUF/resolve/main/smollm-135m-instruct.Q4_K_M.gguf
```

### 5.3 选择建议

**Target: TinyLlama-1.1B**
- **Draft**: TinyLlama-160M（参数比 1:7，速度快 5-7x）
- **预期接受率**: 40-60%
- **预期加速比**: 1.8-2.5x

---

## 6. 实验设计

### 6.1 对比维度

| 维度 | 对照组 | 实验组 |
|------|--------|--------|
| **方法** | 传统自回归 | 推测式解码 |
| **方法** | KV-Cache 前缀缓存 | 推测式解码 + KV-Cache |

### 6.2 测试场景

1. **代码生成**（高可预测性）
   - Prompt: "用Python实现快速排序"
   - 预期：接受率高（70%+）

2. **对话问答**（中等可预测性）
   - Prompt: "什么是机器学习？"
   - 预期：接受率中等（50-60%）

3. **创意写作**（低可预测性）
   - Prompt: "写一首关于秋天的诗"
   - 预期：接受率低（30-40%）

### 6.3 指标收集

```python
# 测试脚本伪代码
results = {
    "scenario": "code_generation",
    "method": "speculative",
    "draft_model": "TinyLlama-160M",
    "n_draft": 16,
    "n_predict": 100,
    "n_drafted": 1600,
    "n_accepted": 960,
    "accept_rate": 0.6,
    "time_normal": 5.2,  # 秒
    "time_spec": 2.1,    # 秒
    "speedup": 2.48
}
```

---

## 7. 参考资料

### 7.1 论文

1. **Fast Inference from Transformers via Speculative Decoding**
   Chen et al., 2023 (Google)
   [arXiv:2211.17192](https://arxiv.org/abs/2211.17192)

2. **Accelerating Large Language Model Decoding with Speculative Sampling**
   Leviathan et al., 2023 (DeepMind)
   [arXiv:2302.01318](https://arxiv.org/abs/2302.01318)

3. **SpecInfer: Accelerating Generative LLM Serving with Speculative Inference**
   Miao et al., 2023 (CMU)
   [arXiv:2305.09781](https://arxiv.org/abs/2305.09781)

### 7.2 llama.cpp 资源

- **Speculative Decoding 示例**：
  `llama.cpp/examples/speculative-simple/speculative-simple.cpp`

- **API 文档**：
  `llama.cpp/llama.h`

- **社区讨论**：
  https://github.com/ggerganov/llama.cpp/discussions/2926

### 7.3 博客与教程

- **Jay Alammar**: [Illustrated Guide to Speculative Decoding](https://jalammar.github.io/illustrated-gpt2/)
- **Hugging Face**: [Speculative Decoding Tutorial](https://huggingface.co/blog/assisted-generation)

---

## 8. 预期成果

### 8.1 技术成果

- ✅ 完整的推测式解码实现
- ✅ 与现有 KV-Cache 优化兼容
- ✅ Docker 一键部署
- ✅ 性能测试框架

### 8.2 实验数据

- ✅ 加速比：1.5x - 2.5x（不同场景）
- ✅ 接受率：40% - 70%（不同场景）
- ✅ 与 KV-Cache 组合效果

### 8.3 论文贡献

- **对比实验**：推测式解码 vs KV-Cache vs 组合方案
- **创新点**：边缘设备上的推测式解码优化
- **实用价值**：在消费级硬件上实现实时 LLM 推理

---

**下一步行动**：
1. ✅ 完成理论研究（本文档）
2. ⏳ 下载并测试 draft 模型
3. ⏳ 实现 `SpeculativeDecoder` 类
4. ⏳ 集成到 ModelManager
5. ⏳ 性能测试与对比

**预计完成时间**：2 周
