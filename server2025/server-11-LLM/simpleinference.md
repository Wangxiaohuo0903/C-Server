# SimpleInference 代码详解与 llama.cpp 推理流程

> 本文档主要是对 `SimpleInference.h` 和llama.cpp 推理流程、核心 API、以及代码实现细节进行了深入补充。

---

## 一、llama.cpp 四层对象模型

一次推理涉及 4 层核心对象，理解它们的关系是读懂代码的前提：

| 对象        | 类型             | 生命周期         | 本质                                            |
| ----------- | ---------------- | ---------------- | ----------------------------------------------- |
| **Model**   | `llama_model*`   | 整个程序         | 权重 + 词表 + 结构超参数                        |
| **Context** | `llama_context*` | 一次会话         | 运行时状态：KV Cache + 推理配置                 |
| **Batch**   | `llama_batch`    | 一次 decode 调用 | 本轮要计算的 token 集合（含位置、序列等元信息） |
| **Sampler** | 采样逻辑         | 生成阶段         | logits → token 的选择策略（贪婪/随机等）        |



### Context 深入理解

Context 的核心是 **KV Cache**。Transformer 的 Attention 机制在处理每个新 token 时，需要和之前**所有 token** 计算注意力。如果不缓存，每步都要重算全部历史的 K、V 向量，代价巨大。

```
Context 内部（简化视图）：

┌─────────────────────────────────────────────────────┐
│  Layer 0:  K=[k0, k1, k2, ..., kn]  V=[v0, v1, ..., vn]  │
│  Layer 1:  K=[k0, k1, k2, ..., kn]  V=[v0, v1, ..., vn]  │
│  ...                                                       │
│  Layer 31: K=[k0, k1, k2, ..., kn]  V=[v0, v1, ..., vn]  │
│                                                             │
│  n_ctx = 2048 （最多能存 2048 个 token 的 KV）              │
└─────────────────────────────────────────────────────┘
```

有了 KV Cache，生成新 token 时只需：
1. 计算新 token 自己的 Q（Query）向量
2. 和已缓存的所有 K 做点积 → 注意力权重
3. 用权重对所有 V 加权求和 → 输出

这就是为什么 `llama_decode` 每调用一次，都会**往 KV Cache 里追加**当前 token 的 K、V。

### Batch 

Batch 是你一次喂给 `llama_decode()` 的 token 集合，本质是一张表格：

```
假设 prompt = "Hello world" → tokens = [15496, 995]

Prefill 阶段的 batch（一次喂 2 个 token）：
┌───────┬──────────┬─────┬────────┬────────┐
│ index │ token    │ pos │ seq_id │ logits │
├───────┼──────────┼─────┼────────┼────────┤
│   0   │  15496   │  0  │   0    │   0    │  ← "Hello"，不需要 logits
│   1   │    995   │  1  │   0    │   1    │  ← "world"，需要 logits（最后一个）
└───────┴──────────┴─────┴────────┴────────┘
```

各字段含义：
- **`token[i]`**：token id，就是词表索引号
- **`pos[i]`**：位置编号，RoPE 位置编码依赖它，必须严格递增
- **`seq_id[i]`**：序列编号，单用户单对话固定为 0；多用户并行时各自不同
- **`logits[i]`**：设为 1 表示"需要这个位置的输出分布"，设为 0 表示"只需更新 KV Cache"

### Logits 数组深入理解

`llama_decode` 执行后，对于 `logits[i]=1` 的位置，模型会输出一个**长度为 vocab_size 的 float 数组**：

```
llama_get_logits(ctx) 返回：

index:   0      1      2     ...   258    ...   32000
       [-3.2,  -1.5,  -0.8, ...,   8.7,  ...,  -2.1]
                                    ↑
                            这个值最大 → argmax → 选它作为 next token
```

- 每个元素对应词表中一个 token，值越大 = 模型认为该 token 越可能是"下一个"
- 这是**未归一化分数**（不是概率），做 softmax 后才变成概率分布
- 贪婪采样只需 argmax，不需要 softmax（因为 softmax 前后 argmax 结果相同）

---

## 二、SimpleInference 类总览

### 设计目标

将 llama.cpp 复杂的 C API 封装为两步调用：

```cpp
SimpleInference inference;
inference.loadModel("model.gguf");                     // 步骤1：加载模型
std::string response = inference.generate("你好", 64); // 步骤2：一键生成
```

### 类结构

```cpp
class SimpleInference {
private:
    llama_model* model_;              // 模型指针（只读资产）
    bool has_chat_template_ = false;  // 模型是否内嵌聊天模板

    // 私有工具方法
    std::string applyChatTemplate(...);                // 渲染聊天模板
    std::vector<llama_token> tokenize(...);            // 文本 → token 序列
    bool process_prompt(llama_context*, ...);          // prefill 阶段
    std::string generate_tokens(llama_context*, ...);  // decode 循环

public:
    SimpleInference();
    ~SimpleInference();                                // RAII：析构时释放 model
    bool loadModel(const std::string& model_path);     // 加载 GGUF 模型
    std::string generate(const std::string& prompt,    // 单轮推理入口
                         int max_tokens = 64,
                         bool use_chat_template = true,
                         const std::string& system_prompt = "");
};
```

### 单轮推理的完整数据流

```
用户文本 "The sky is"
    │
    ▼ applyChatTemplate()  ← 可选：加角色标记
"<|im_start|>user\nThe sky is<|im_end|>\n<|im_start|>assistant\n"
    │
    ▼ tokenize()           ← llama_tokenize()
token id 序列 [151644, 872, 198, 791, 13180, 374, ...]
    │
    ▼ process_prompt()     ← llama_decode(batch of N)
KV Cache 已填充，最后一个位置的 logits 就绪
    │
    ▼ generate_tokens() 循环  ← llama_get_logits → argmax → llama_decode(batch of 1)
    │    step 0: logits → argmax → "blue"  → decode → 更新 KV Cache
    │    step 1: logits → argmax → "."     → decode → 更新 KV Cache
    │    step 2: logits → argmax → EOS     → 停止
    │
    ▼
输出字符串 "blue."
```

---

## 三、各函数详解与对应 llama.cpp API

### 3.1 构造函数与析构函数

```cpp
SimpleInference() : model_(nullptr) {}

~SimpleInference() {
    if (model_) {
        llama_model_free(model_);  // 释放模型内存
    }
}
```

**RAII 原则**：资源在构造时获取（`loadModel`），在析构时自动释放。即使中途抛异常，析构函数也会被调用，防止内存泄漏。

| 调用的 llama 函数         | 功能                                       |
| ------------------------- | ------------------------------------------ |
| `llama_model_free(model)` | 释放模型对象占用的所有内存（权重、词表等） |

---

### 3.2 loadModel() — 加载 GGUF 模型

**功能**：从 GGUF 文件读取模型权重、词表、元数据到内存，并检查是否内嵌聊天模板。

```cpp
bool loadModel(const std::string &model_path) {
    // 1. 释放旧模型（如有）
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }

    // 2. 配置加载参数
    llama_model_params model_params = llama_model_default_params();
    model_params.use_mmap = false;   // 禁用 mmap（macOS Docker 兼容）
    model_params.n_gpu_layers = 0;   // 纯 CPU 模式

    // 3. 加载模型
    model_ = llama_model_load_from_file(model_path.c_str(), model_params);

    // 4. 检查 chat template
    const char *tmpl = llama_model_chat_template(model_, nullptr);
    has_chat_template_ = (tmpl != nullptr);

    return (model_ != nullptr);
}
```

**调用的 llama 函数一览**：

| 函数                                        | 功能                     | 说明                                                              |
| ------------------------------------------- | ------------------------ | ----------------------------------------------------------------- |
| `llama_model_default_params()`              | 返回模型加载参数的默认值 | 包含 mmap、GPU offload 层数、mlock 等选项                         |
| `llama_model_load_from_file(path, params)`  | 从 GGUF 文件加载模型     | 返回 `llama_model*`，这是只读资产，可被多个 context 共享          |
| `llama_model_chat_template(model, nullptr)` | 获取模型内嵌的聊天模板   | 模板定义了如何将对话消息渲染成 prompt（如 ChatML、Llama3 格式等） |
| `llama_model_free(model)`                   | 释放模型                 | 释放权重、词表等占用的内存                                        |

**关键参数说明**：

```cpp
model_params.use_mmap = false;
// mmap（内存映射文件）让操作系统按需加载模型页面，节省内存。
// 但在 macOS Docker (Apple Silicon) 上可能导致挂起，因此禁用。

model_params.n_gpu_layers = 0;
// 控制多少层 Transformer 放到 GPU 上计算。
// 0 = 纯 CPU 推理；-1 = 全部放 GPU（如果有的话）。
```

---

### 3.3 generate() — 单轮推理主函数

**功能**：完成一次完整的"用户输入 → 模型回答"流程。这是整个类的核心调度函数。

```cpp
std::string generate(const std::string &prompt,
                     int max_tokens = 64,
                     bool use_chat_template = true,
                     const std::string &system_prompt = "")
```

**执行步骤与对应 llama 函数**：

#### Step 1: 构建最终 prompt

```cpp
std::string full_prompt = prompt;
if (use_chat_template && has_chat_template_) {
    full_prompt = applyChatTemplate(prompt, system_prompt);
    // 失败则降级为原始 prompt
}
```

这一步决定喂给模型的"最终文本"是什么。如果模型有 chat template，就按模板渲染；否则直接用原始文本。

#### Step 2: 创建推理上下文

```cpp
llama_context_params ctx_params = llama_context_default_params();
ctx_params.n_ctx = 2048;      // 上下文长度上限
ctx_params.n_threads = 4;     // CPU 推理线程数
ctx_params.n_batch = 512;     // prefill 一次最多处理的 token 数

llama_context *ctx = llama_init_from_model(model_, ctx_params);
```

| 函数                                   | 功能                                                            |
| -------------------------------------- | --------------------------------------------------------------- |
| `llama_context_default_params()`       | 返回 context 参数默认值                                         |
| `llama_init_from_model(model, params)` | 基于已加载的 model 创建推理上下文，**内部会分配 KV Cache 内存** |

**参数影响**：

| 参数            | 影响                                         | 建议                                      |
| --------------- | -------------------------------------------- | ----------------------------------------- |
| `n_ctx = 2048`  | KV Cache 能容纳的最大 token 数。越大越占内存 | 按应用场景选，短对话 2048 足够            |
| `n_threads = 4` | CPU 并行线程数                               | 建议 ≤ 物理核心数，过多反而因线程切换变慢 |
| `n_batch = 512` | prefill 阶段单次最多处理 token 数            | 影响 prefill 吞吐，一般 512 即可          |

#### Step 3: Tokenize

```cpp
const llama_vocab *vocab = llama_model_get_vocab(model_);
std::vector<llama_token> tokens = tokenize(vocab, full_prompt);
```

| 函数                           | 功能                                                  |
| ------------------------------ | ----------------------------------------------------- |
| `llama_model_get_vocab(model)` | 获取模型的词表对象，后续 tokenize/detokenize 都需要它 |

#### Step 4-5: Prefill → Decode Loop

```cpp
process_prompt(ctx, tokens);                                    // prefill
std::string output = generate_tokens(ctx, vocab, max_tokens, tokens.size()); // decode
```

#### Step 6: 释放 Context

```cpp
llama_free(ctx);
```

| 函数              | 功能                                                                                          |
| ----------------- | --------------------------------------------------------------------------------------------- |
| `llama_free(ctx)` | 释放 context 及其 KV Cache。**每次 generate 都新建+释放 context，所以是单轮对话，不保留历史** |

> **为什么是单轮对话？** 因为 KV Cache 随 context 释放而清空。要做多轮对话，需要复用同一个 context，让 KV Cache 在轮次间保持。

---

### 3.4 applyChatTemplate() — 渲染聊天模板

**功能**：将 system/user 消息按照模型内置的模板格式，渲染成最终 prompt 字符串。

**为什么需要 chat template？** 不同模型对"对话格式"有不同要求。例如：

| 模型            | 格式示例                                                                            |
| --------------- | ----------------------------------------------------------------------------------- |
| ChatML (Qwen等) | `<\|im_start\|>user\n你好<\|im_end\|>\n<\|im_start\|>assistant\n`                   |
| Llama3          | `<\|begin_of_text\|><\|start_header_id\|>user<\|end_header_id\|>\n你好<\|eot_id\|>` |

如果不按模型要求的格式输入，模型的输出质量会大幅下降。chat template 就是把这层格式差异封装起来。

```cpp
std::string applyChatTemplate(const std::string &user_message,
                              const std::string &system_prompt = "")
{
    // 1. 构造消息数组
    std::vector<llama_chat_message> messages;
    if (!system_prompt.empty()) {
        messages.push_back({"system", system_prompt.c_str()});
    }
    messages.push_back({"user", user_message.c_str()});

    // 2. 分配输出缓冲区
    std::vector<char> buffer(total_chars * 2 + 1024);

    // 3. 调用渲染函数
    int32_t result = llama_chat_apply_template(
        nullptr,              // tmpl=nullptr → 使用模型默认模板
        messages.data(),      // 消息数组
        (int)messages.size(), // 消息数量
        true,                 // add_ass=true → 末尾追加 assistant 起始标记
        buffer.data(),        // 输出缓冲区
        (int)buffer.size()    // 缓冲区大小
    );

    // 4. 若缓冲区不够，扩容后重试
    if (result > (int32_t)buffer.size()) {
        buffer.resize(result + 1);
        result = llama_chat_apply_template(/* 同上参数 */);
    }

    return std::string(buffer.data(), (size_t)result);
}
```

| 函数                                                               | 功能                                 |
| ------------------------------------------------------------------ | ------------------------------------ |
| `llama_chat_apply_template(tmpl, msgs, n, add_ass, buf, buf_size)` | 将消息数组按模板渲染成 prompt 字符串 |

**参数解释**：
- `tmpl = nullptr`：使用模型 GGUF 元数据里的默认模板
- `add_ass = true`：在末尾追加 assistant 角色的起始标记，提示模型"该你回答了"
- **返回值**：>0 为实际写入字节数；若大于 buf_size 表示需要扩容；<0 表示失败

**渲染前后对比**（以 ChatML 为例）：

```
渲染前（原始消息）：
  system: "你是一个助手"
  user:   "你好"

渲染后（full_prompt）：
  <|im_start|>system
  你是一个助手<|im_end|>
  <|im_start|>user
  你好<|im_end|>
  <|im_start|>assistant
```

---

### 3.5 tokenize() — 文本转 Token 序列

**功能**：将 UTF-8 文本转换为 token id 序列（数字数组）。

```cpp
std::vector<llama_token> tokenize(const llama_vocab *vocab, const std::string &text)
{
    // 预估 buffer 大小（text.size()*4 是偏大的安全估计）
    std::vector<llama_token> tokens(text.size() * 4);

    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        (int)text.size(),
        tokens.data(),
        (int)tokens.size(),
        true,   // add_special：自动添加 BOS 等特殊 token
        false   // parse_special：不把 "<|...|>" 当特殊 token 解析
    );

    // 返回负数 → buffer 不够，扩容重试
    if (n_tokens < 0) {
        tokens.resize((size_t)(-n_tokens));
        n_tokens = llama_tokenize(/* 同上参数 */);
    }

    tokens.resize((size_t)n_tokens);
    return tokens;
}
```

| 函数                                                                                    | 功能                       |
| --------------------------------------------------------------------------------------- | -------------------------- |
| `llama_tokenize(vocab, text, text_len, tokens, max_tokens, add_special, parse_special)` | 将文本切分为 token id 序列 |

**参数详解**：

| 参数                    | 值                                                      | 含义 |
| ----------------------- | ------------------------------------------------------- | ---- |
| `add_special = true`    | 允许 tokenizer 按模型规则添加特殊 token（如 BOS）       |
| `parse_special = false` | 不解析文本中的 `<\|...\|>` 为特殊 token，按普通文字处理 |

**返回值约定**：
- `> 0`：实际 token 数
- `< 0`：buffer 不够大，`-返回值` = 所需的 token 数

**为什么预估 `text.size() * 4`？**
- 英文：token 数通常 < 字符数（"Hello" = 1 token, 5 chars）
- 中文：一个汉字可能对应 1~3 个 token（取决于词表）
- byte-fallback：极端情况下每字节变一个 token
- `*4` 是教学示例中常见的保守估计

---

### 3.6 process_prompt() — Prefill 阶段

**功能**：把全部 prompt tokens 一次性喂给模型，执行前向计算，填充 KV Cache。这是推理的"准备阶段"。

```cpp
bool process_prompt(llama_context *ctx, const std::vector<llama_token> &tokens)
{
    // 1. 初始化 batch，容量 = prompt token 数
    llama_batch batch = llama_batch_init((int)tokens.size(), 0, 1);

    // 2. 填充 batch 表格
    for (size_t i = 0; i < tokens.size(); ++i) {
        batch.token[i]      = tokens[i];           // token id
        batch.pos[i]        = (int)i;              // 位置：0, 1, 2, ...
        batch.seq_id[i][0]  = 0;                   // 单序列 id=0
        batch.n_seq_id[i]   = 1;                   // 属于 1 条序列

        // 关键：只对最后一个 token 请求 logits
        batch.logits[i] = (i == tokens.size() - 1) ? 1 : 0;
    }
    batch.n_tokens = (int)tokens.size();

    // 3. 执行前向计算
    int ret = llama_decode(ctx, batch);

    // 4. 释放 batch
    llama_batch_free(batch);

    return ret == 0;
}
```

| 函数                                          | 功能                                                                                                       |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| `llama_batch_init(n_tokens, embd, n_seq_max)` | 分配 batch 结构体，容量为 n_tokens                                                                         |
| `llama_decode(ctx, batch)`                    | **核心函数**：执行一次 Transformer 前向传播，将 tokens 写入 KV Cache，并计算标记为需要 logits 的位置的输出 |
| `llama_batch_free(batch)`                     | 释放 batch 内部分配的内存                                                                                  |

**为什么只对最后一个 token 设 `logits[i] = 1`？**

```
假设 prompt = [t0, t1, t2, t3, t4]

Prefill 后 KV Cache：
  Layer 0: K=[k0,k1,k2,k3,k4]  V=[v0,v1,v2,v3,v4]
  ...

我们只需要知道"t4 之后该接什么"，
所以只需 t4 位置的 logits → 预测 t5。
t0~t3 的 logits 对我们没用，不计算 = 节省算力和内存带宽。
```

**llama_decode 做了什么？**

这是整个推理最核心的函数，一次调用完成：
1. 将 batch 中所有 token 的 embedding 送入 Transformer
2. 逐层执行 Self-Attention + FFN（利用已有 KV Cache 加速）
3. 将当前 batch 中所有 token 产生的 K、V 追加到 KV Cache
4. 对 `logits[i]=1` 的位置，计算并存储 logits 数组（vocab_size 维）
5. 返回 0 表示成功

---

### 3.7 generate_tokens() — Decode 循环（自回归生成）

**功能**：Prefill 完成后，进入自回归循环，每步生成一个 token，直到遇到 EOS 或达到 max_tokens。

```cpp
std::string generate_tokens(llama_context *ctx,
                            const llama_vocab *vocab,
                            int max_tokens,
                            int n_past_init)
{
    std::string output;
    const int eos_token  = llama_vocab_eos(vocab);
    const int vocab_size = llama_vocab_n_tokens(vocab);

    llama_batch gen_batch = llama_batch_init(1, 0, 1);  // 容量=1
    int n_past = n_past_init;  // 下一个 token 的 position

    for (int step = 0; step < max_tokens; ++step) {

        // ① 取 logits
        const float *logits = llama_get_logits(ctx);

        // ② 贪婪采样（argmax）
        int next_token = 0;
        float max_logit = logits[0];
        for (int i = 1; i < vocab_size; ++i) {
            if (logits[i] > max_logit) {
                max_logit = logits[i];
                next_token = i;
            }
        }

        // ③ 检查 EOS
        if (next_token == eos_token) break;

        // ④ Detokenize：token → 文本片段
        char piece[256] = {0};
        int len = llama_token_to_piece(vocab, next_token,
                                        piece, sizeof(piece), 0, false);
        if (len > 0) output.append(piece, len);

        // ⑤ 把新 token 喂回模型
        gen_batch.n_tokens     = 1;
        gen_batch.token[0]     = next_token;
        gen_batch.pos[0]       = n_past;       // position 必须递增
        gen_batch.seq_id[0][0] = 0;
        gen_batch.n_seq_id[0]  = 1;
        gen_batch.logits[0]    = 1;            // 需要下一步的 logits

        llama_decode(ctx, gen_batch);  // 更新 KV Cache + 产生新 logits
        n_past++;
    }

    llama_batch_free(gen_batch);
    return output;
}
```

**调用的 llama 函数一览**：

| 函数                                                             | 功能                                                        |
| ---------------------------------------------------------------- | ----------------------------------------------------------- |
| `llama_get_logits(ctx)`                                          | 获取上一次 decode 产生的 logits（vocab_size 的 float 数组） |
| `llama_vocab_eos(vocab)`                                         | 获取 EOS token 的 id，用于判断生成结束                      |
| `llama_vocab_n_tokens(vocab)`                                    | 获取词表大小，用于遍历 logits 做 argmax                     |
| `llama_token_to_piece(vocab, token, buf, size, lstrip, special)` | Detokenize：将单个 token id 转为 UTF-8 文本片段             |
| `llama_decode(ctx, batch)`                                       | 同 prefill，但此时 batch 只有 1 个 token                    |
| `llama_batch_init / llama_batch_free`                            | 分配/释放 batch                                             |

**逐步执行图示**：

```
Prefill 完成后状态：
  KV Cache: [t0, t1, ..., t99]  （100 个 prompt tokens）
  logits 就绪 → 预测第 101 个 token

Step 0:
  logits → argmax → token_100 = "blue"
  detokenize → output += "blue"
  gen_batch = [token_100, pos=100, logits=1]
  llama_decode → KV Cache: [t0...t99, t100]
  新 logits 就绪

Step 1:
  logits → argmax → token_101 = "."
  detokenize → output += "."
  gen_batch = [token_101, pos=101, logits=1]
  llama_decode → KV Cache: [t0...t100, t101]
  新 logits 就绪

Step 2:
  logits → argmax → EOS
  break!

最终输出: "blue."
```

**采样策略说明**：

本代码使用**贪婪采样（Greedy Sampling）**——每次取 logits 中最大值的 token。这是最简单的策略：

```
logits: [-3.2, -1.5, -0.8, ..., 8.7, ..., -2.1]
                                  ↑
                          max → 选这个 token

特点：输出稳定、确定性强，但缺乏创造力和多样性。
```

其他常见采样策略（代码中未实现，但在完整 llama.cpp sampler chain 中可用）：

| 策略            | 原理                                       | 效果               |
| --------------- | ------------------------------------------ | ------------------ |
| Temperature     | 除以温度系数再做 softmax，T>1 使分布更平坦 | 增加随机性和创造力 |
| Top-K           | 只保留概率最高的 K 个 token                | 过滤低概率噪声     |
| Top-P (Nucleus) | 保留累积概率达到 P 的最小 token 集合       | 自适应过滤         |
| Repeat Penalty  | 降低已出现 token 的 logits                 | 减少复读           |

---

## 四、llama.cpp 核心 API 完整速查表

以下是 SimpleInference 代码中使用到的所有 llama.cpp 函数，按调用顺序排列：

### 模型加载/释放

| API                          | 原型                                                                       | 功能                               |
| ---------------------------- | -------------------------------------------------------------------------- | ---------------------------------- |
| `llama_model_default_params` | `llama_model_params llama_model_default_params()`                          | 返回模型加载参数默认值             |
| `llama_model_load_from_file` | `llama_model* llama_model_load_from_file(const char*, llama_model_params)` | 从 GGUF 加载模型，返回只读模型对象 |
| `llama_model_chat_template`  | `const char* llama_model_chat_template(llama_model*, const char*)`         | 获取模型内嵌的聊天模板字符串       |
| `llama_model_get_vocab`      | `const llama_vocab* llama_model_get_vocab(llama_model*)`                   | 获取模型词表对象                   |
| `llama_model_free`           | `void llama_model_free(llama_model*)`                                      | 释放模型                           |

### Context 创建/释放

| API                            | 原型                                                                       | 功能                            |
| ------------------------------ | -------------------------------------------------------------------------- | ------------------------------- |
| `llama_context_default_params` | `llama_context_params llama_context_default_params()`                      | 返回 context 参数默认值         |
| `llama_init_from_model`        | `llama_context* llama_init_from_model(llama_model*, llama_context_params)` | 创建推理上下文（分配 KV Cache） |
| `llama_free`                   | `void llama_free(llama_context*)`                                          | 释放 context 及 KV Cache        |

### 聊天模板

| API                         | 原型                                                                                         | 功能                               |
| --------------------------- | -------------------------------------------------------------------------------------------- | ---------------------------------- |
| `llama_chat_apply_template` | `int32_t llama_chat_apply_template(const char*, llama_chat_message*, int, bool, char*, int)` | 按模板渲染消息数组为 prompt 字符串 |

### Tokenize / Detokenize

| API                    | 原型                                                                                      | 功能                       |
| ---------------------- | ----------------------------------------------------------------------------------------- | -------------------------- |
| `llama_tokenize`       | `int llama_tokenize(const llama_vocab*, const char*, int, llama_token*, int, bool, bool)` | 文本 → token id 序列       |
| `llama_token_to_piece` | `int llama_token_to_piece(const llama_vocab*, llama_token, char*, int, int, bool)`        | 单个 token id → UTF-8 文本 |
| `llama_vocab_eos`      | `int llama_vocab_eos(const llama_vocab*)`                                                 | 获取 EOS token id          |
| `llama_vocab_n_tokens` | `int llama_vocab_n_tokens(const llama_vocab*)`                                            | 获取词表大小               |

### Batch 与 Decode

| API                | 原型                                            | 功能                                               |
| ------------------ | ----------------------------------------------- | -------------------------------------------------- |
| `llama_batch_init` | `llama_batch llama_batch_init(int, int, int)`   | 分配 batch 结构体                                  |
| `llama_decode`     | `int llama_decode(llama_context*, llama_batch)` | **核心**：执行前向传播，更新 KV Cache，计算 logits |
| `llama_get_logits` | `float* llama_get_logits(llama_context*)`       | 获取上一次 decode 输出的 logits 数组               |
| `llama_batch_free` | `void llama_batch_free(llama_batch)`            | 释放 batch                                         |

---

## 五、`llama_decode` 调用次数分析

理解 `llama_decode` 的调用模式是理解推理性能的关键：

```
假设 prompt 有 100 个 tokens，max_tokens = 50

Prefill 阶段：llama_decode 调用 1 次
  → 输入 batch 大小 = 100（一次处理所有 prompt tokens）
  → KV Cache: 100 条记录

Decode 阶段：llama_decode 最多调用 50 次
  → 每次输入 batch 大小 = 1（只喂刚生成的 token）
  → KV Cache: 从 101 增长到 150

总计：1 + 50 = 最多 51 次 llama_decode 调用
```

**性能特点**：
- **Prefill**：一次处理大量 token，计算密集型（compute-bound），可充分利用并行
- **Decode**：每次只处理 1 个 token，受内存带宽限制（memory-bound），因为每步都要读取全部模型参数做一次矩阵乘法

这也是为什么"首 token 延迟"（Time To First Token, TTFT）和"逐 token 速度"（Tokens Per Second, TPS）是两个不同的性能指标。

---

## 六、Server-11 的局限性与改进方向

### 当前局限

| 问题           | 原因                                 | 表现                                                 |
| -------------- | ------------------------------------ | ---------------------------------------------------- |
| **无对话记忆** | 每次 `generate()` 新建并释放 context | 第 1 轮说"我叫 Alice"，第 2 轮就忘了                 |
| **重复计算**   | 不保留 KV Cache                      | 多轮对话时，每轮都要重新 tokenize + prefill 全部历史 |
| **单线程阻塞** | 所有请求共享一个推理实例             | 用户 A 在推理时，用户 B 必须排队等待                 |
| **仅贪婪采样** | 没有 sampler chain                   | 输出固定，缺乏多样性                                 |

### 改进方向（Server-12 预告）

- **SessionManager**：为每个用户维护独立 context，复用 KV Cache 实现多轮对话
- **Sampler Chain**：引入 temperature / top-k / top-p / repeat penalty 等采样策略
- **并发推理**：多 context 并行服务多用户
- **流式输出（Streaming）**：逐 token 发送给客户端，降低用户感知延迟
