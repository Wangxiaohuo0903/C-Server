# Server-11 课程文档：LLM 集成与单轮推理

## 铺垫概念

### 1. 大语言模型（LLM）基础
- **定义**：Large Language Model，基于深度学习的文本生成模型。
- **代表模型**：
  - GPT 系列（OpenAI）
  - LLaMA 系列（Meta）
  - Qwen、ChatGLM（国产）
- **核心能力**：
  - 文本生成：根据输入生成连贯的文本
  - 对话理解：理解用户意图并回复
  - 知识问答：基于训练数据回答问题

### 2. 模型量化（Quantization）
- **定义**：将模型参数从高精度（如 FP32）转换为低精度（如 INT8、INT4）。
- **目的**：
  - **减少内存占用**：7B 模型从 28GB（FP32）压缩到 4GB（Q4）
  - **加快推理速度**：低精度计算更快
  - **支持消费级硬件**：普通电脑也能运行

**量化格式对比**：
```
FP32（原始）: 7B × 4 bytes = 28GB
FP16（半精度）: 7B × 2 bytes = 14GB
INT8（8位整数）: 7B × 1 byte = 7GB
Q4（4位量化）: 7B × 0.5 bytes = 3.5GB
```

### 3. GGUF 格式
- **全称**：GPT-Generated Unified Format
- **特点**：
  - llama.cpp 的标准模型格式
  - 单文件包含模型权重、配置、词表
  - 支持多种量化级别（Q4_K_M、Q5_K_M、Q8_0 等）
- **命名规范**：
  ```
  model_name-size-quant.gguf
  示例：tinyllama-1.1B-Q4_K_M.gguf
        qwen2.5-1.5B-Q5_K_M.gguf
  ```

---

## 为什么要集成 LLM

### Server-10 的局限性
Server-10 只有 RESTful API 和数据库功能，无法提供 AI 对话能力：
- 用户注册/登录 ✓
- 数据存储 ✓
- AI 对话 ✗（缺失）

### 集成 LLM 的价值
1. **智能问答**：用户可以向服务器提问，获得 AI 回复
2. **内容生成**：自动生成文章、代码、翻译等
3. **交互体验**：从传统 Web 服务升级为 AI 助手
4. **完整闭环**：用户管理 + AI 推理 + 数据持久化

### 应用场景
- **客服机器人**：自动回答用户问题
- **写作助手**：辅助创作文章、邮件
- **代码补全**：智能代码生成
- **知识问答**：企业知识库查询

---

## llama.cpp 简介

### 什么是 llama.cpp？
- **定义**：用纯 C/C++ 实现的 LLM 推理引擎
- **作者**：Georgi Gerganov（保加利亚开发者）
- **开源**：https://github.com/ggerganov/llama.cpp
- **特点**：
  - 无需 Python 环境
  - 支持 CPU 推理（无需 GPU）
  - 高度优化（SIMD、多线程）
  - 跨平台（Windows、Linux、macOS）

### llama.cpp vs PyTorch/Transformers
```
PyTorch/Transformers（Python生态）:
  - 优点：灵活、生态丰富、易于研究
  - 缺点：依赖 Python、内存占用高、部署复杂

llama.cpp（C++）:
  - 优点：轻量、快速、易部署、CPU友好
  - 缺点：功能相对受限、不适合训练
```

**选择 llama.cpp 的原因**：
- 服务器场景需要高性能和低资源占用
- C++ 项目集成更自然
- 无需额外安装 Python 依赖

### llama.cpp 核心 API

```cpp
// 1. 模型加载
llama_model* llama_model_load_from_file(
    const char* path,             // GGUF 模型文件路径
    llama_model_params params     // 模型参数（如 GPU层数）
);

// 2. 创建推理上下文
llama_context* llama_new_context_with_model(
    llama_model* model,
    llama_context_params params   // 上下文参数（如 n_ctx、n_threads）
);

// 3. Tokenize（文本 → Token IDs）
int llama_tokenize(
    const llama_vocab* vocab,
    const char* text,             // 输入文本
    llama_token* tokens,          // 输出 token 数组
    int max_tokens,               // 最大 token 数
    bool add_special,             // 是否添加特殊 token（如BOS）
    bool parse_special            // 是否解析特殊 token
);

// 4. 推理（前向传播）
int llama_decode(
    llama_context* ctx,
    llama_batch batch             // 包含 tokens、positions 等信息
);

// 5. 获取输出 logits
float* llama_get_logits_ith(
    llama_context* ctx,
    int i                         // batch 中的索引
);

// 6. Token → 文本
int llama_token_to_piece(
    const llama_vocab* vocab,
    llama_token token,            // Token ID
    char* buf,                    // 输出缓冲区
    int length,                   // 缓冲区长度
    int lstrip,                   // 左侧去除空格数
    bool special                  // 是否渲染特殊 token
);

// 7. 释放资源
void llama_free(llama_context* ctx);
void llama_model_free(llama_model* model);
```

---

## SimpleInference 类设计

### 设计目标
封装 llama.cpp 的复杂 API，提供简单易用的推理接口：
```cpp
// 简化的调用方式
SimpleInference inference;
inference.loadModel("model.gguf");
std::string response = inference.generate("你好", 64);
```

### 核心数据结构

```cpp
class SimpleInference {
private:
    llama_model* model_;      // 模型指针
    llama_context* ctx_;      // 推理上下文
    bool initialized_;        // 是否已初始化

public:
    // 加载模型
    bool loadModel(const std::string& model_path, int n_ctx = 2048, int n_threads = 4);

    // 生成回复
    std::string generate(const std::string& prompt, int max_tokens = 64);

    // 析构函数（自动释放资源）
    ~SimpleInference();
};
```

### 代码实现详解

#### 1. loadModel() - 模型加载

```cpp
bool SimpleInference::loadModel(const std::string& model_path, int n_ctx, int n_threads) {
    // 1. 初始化 llama 后端（一次性设置）
    llama_backend_init();

    // 2. 设置模型参数
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;  // CPU 推理（设为 -1 可启用 GPU）

    // 3. 加载模型
    model_ = llama_model_load_from_file(model_path.c_str(), model_params);
    if (!model_) {
        std::cerr << "[SimpleInference] 模型加载失败: " << model_path << std::endl;
        return false;
    }
    std::cout << "[SimpleInference] 模型加载成功\n";

    // 4. 设置上下文参数
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = n_ctx;        // 上下文长度（最大输入+输出 tokens）
    ctx_params.n_threads = n_threads; // CPU 线程数

    // 5. 创建推理上下文
    ctx_ = llama_new_context_with_model(model_, ctx_params);
    if (!ctx_) {
        std::cerr << "[SimpleInference] 上下文创建失败\n";
        llama_model_free(model_);
        return false;
    }

    initialized_ = true;
    std::cout << "[SimpleInference] 推理引擎初始化完成\n";
    std::cout << "  - 上下文长度: " << n_ctx << "\n";
    std::cout << "  - CPU线程数: " << n_threads << "\n";
    return true;
}
```

**参数说明**：
- `n_ctx`：上下文窗口大小（如 2048）
  - 决定了模型能处理的最大 token 数（输入 + 输出）
  - 越大内存占用越高
- `n_threads`：CPU 线程数
  - 建议设置为物理核心数（如 4-8）
  - 过多线程反而会降低性能

#### 2. generate() - 文本生成

```cpp
std::string SimpleInference::generate(const std::string& prompt, int max_tokens) {
    if (!initialized_ || !ctx_ || !model_) {
        return "[error: not initialized]";
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);
    const int eos_token = llama_vocab_eos(vocab);  // 结束符 token ID
    const int vocab_size = llama_vocab_n_tokens(vocab);

    // ===== 步骤1: Tokenize 输入 prompt =====
    std::vector<llama_token> prompt_tokens(prompt.size() + 128);
    int n_prompt_tokens = llama_tokenize(
        vocab,
        prompt.c_str(),
        prompt_tokens.data(),
        prompt_tokens.size(),
        true,   // add_special = true（添加 BOS token）
        false   // parse_special = false
    );

    if (n_prompt_tokens < 0) {
        return "[error: tokenization failed]";
    }
    prompt_tokens.resize(n_prompt_tokens);

    std::cout << "[SimpleInference] Tokenized prompt: " << n_prompt_tokens << " tokens\n";

    // ===== 步骤2: Prompt 推理（处理所有输入 tokens）=====
    llama_batch batch = llama_batch_init(n_prompt_tokens, 0, 1);
    for (int i = 0; i < n_prompt_tokens; ++i) {
        batch.token[i] = prompt_tokens[i];
        batch.pos[i] = i;              // Token 位置（用于 RoPE 位置编码）
        batch.seq_id[i][0] = 0;        // 序列 ID（单序列推理）
        batch.n_seq_id[i] = 1;
        batch.logits[i] = (i == n_prompt_tokens - 1);  // 只在最后一个 token 计算 logits
    }
    batch.n_tokens = n_prompt_tokens;

    // 执行前向传播
    if (llama_decode(ctx_, batch) != 0) {
        llama_batch_free(batch);
        return "[error: prompt decode failed]";
    }

    // ===== 步骤3: 逐 token 生成 =====
    std::string generated_text;
    int n_generated = 0;

    for (int i = 0; i < max_tokens; ++i) {
        // 3.1 获取 logits（最后一个 token 的输出概率分布）
        const float* logits = llama_get_logits_ith(ctx_, batch.n_tokens - 1);

        // 3.2 采样：选择概率最高的 token（Greedy Sampling）
        int next_token = 0;
        float max_logit = -INFINITY;
        for (int v = 0; v < vocab_size; ++v) {
            if (logits[v] > max_logit) {
                max_logit = logits[v];
                next_token = v;
            }
        }

        // 3.3 检查是否结束
        if (next_token == eos_token) {
            std::cout << "[SimpleInference] EOS token reached\n";
            break;
        }

        // 3.4 Token → 文本
        char piece_buf[256] = {0};
        int n_piece = llama_token_to_piece(vocab, next_token, piece_buf, sizeof(piece_buf), 0, false);
        std::string piece(piece_buf, n_piece > 0 ? n_piece : 0);
        generated_text += piece;
        n_generated++;

        // 3.5 将新 token 加入 batch，准备下一轮推理
        batch.n_tokens = 1;
        batch.token[0] = next_token;
        batch.pos[0] = n_prompt_tokens + n_generated - 1;  // 累积位置
        batch.seq_id[0][0] = 0;
        batch.n_seq_id[0] = 1;
        batch.logits[0] = true;

        // 3.6 推理下一个 token
        if (llama_decode(ctx_, batch) != 0) {
            std::cout << "[SimpleInference] Decode failed at token " << i << "\n";
            break;
        }
    }

    llama_batch_free(batch);
    std::cout << "[SimpleInference] Generated " << n_generated << " tokens\n";
    return generated_text;
}
```

**核心流程**：
```
输入: "你好"
  ↓ Tokenize
Tokens: [101, 102]  (假设BOS=101, 好=102)
  ↓ Prompt推理
Logits: [0.1, 0.3, 0.6, ...]  (vocab_size维向量)
  ↓ 采样（选最大概率）
Next Token: 105
  ↓ Token→文本
Output: "世"
  ↓ 循环生成
最终输出: "世界！很高兴认识你。"
```

---

## Router 集成：/infer-simple 接口

### 接口设计

**HTTP 接口**：
```
POST /infer-simple
Content-Type: application/json

Request:
{
  "prompt": "什么是人工智能？",
  "max_tokens": "128"
}

Response:
{
  "success": true,
  "response": "人工智能（AI）是计算机科学的一个分支..."
}
```

### Router 实现

```cpp
void Router::setupRESTfulRoutes(Database& db, SimpleInference& inference) {
    addRoute("POST", "/infer-simple", [&inference](const HttpRequest& req) {
        // 1. 解析 JSON 请求
        auto params = req.parseJson();
        std::string prompt = params["prompt"];
        std::string max_tokens_str = params["max_tokens"];

        // 2. 参数验证
        if (prompt.empty()) {
            HttpResponse resp;
            resp.setStatusCode(400);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody("{\"success\":false,\"message\":\"Prompt required\"}");
            return resp;
        }

        // 3. 解析 max_tokens（默认 64）
        int max_tokens = 64;
        if (!max_tokens_str.empty()) {
            try {
                max_tokens = std::stoi(max_tokens_str);
            } catch (...) {
                max_tokens = 64;
            }
        }

        std::cout << "[/infer-simple] Generating response for: " << prompt << "\n";

        // 4. 调用推理引擎
        std::string response = inference.generate(prompt, max_tokens);

        std::cout << "[/infer-simple] Response: " << response << "\n";

        // 5. 返回 JSON 响应
        HttpResponse resp;
        resp.setStatusCode(200);
        resp.setHeader("Content-Type", "application/json");
        resp.setBody("{\"success\":true,\"response\":\"" + response + "\"}");
        return resp;
    });
}
```

---

## 本节课用到的 C++ 知识

### 1. Lambda 表达式与捕获列表

```cpp
addRoute("POST", "/infer-simple", [&inference](const HttpRequest& req) {
    // lambda 函数体
});
```

**捕获列表说明**：
```cpp
[]        // 不捕获任何变量
[&]       // 引用捕获所有外部变量
[=]       // 值捕获所有外部变量
[&inference]  // 只引用捕获 inference
[inference]   // 只值捕获 inference
[&, max_tokens] // 引用捕获所有，但 max_tokens 值捕获
```

在这里使用 `[&inference]` 是因为：
- `inference` 是外部变量，lambda 内部需要访问
- 使用引用捕获避免拷贝（`SimpleInference` 不可拷贝）
- 确保 lambda 调用时 `inference` 对象仍然有效

### 2. RAII 资源管理

```cpp
class SimpleInference {
public:
    ~SimpleInference() {
        if (ctx_) {
            llama_free(ctx_);  // 自动释放上下文
            ctx_ = nullptr;
        }
        if (model_) {
            llama_model_free(model_);  // 自动释放模型
            model_ = nullptr;
        }
        llama_backend_free();  // 释放后端资源
    }
};
```

**RAII原则**（Resource Acquisition Is Initialization）：
- **获取**：构造函数中分配资源（loadModel）
- **释放**：析构函数中自动释放资源
- **优势**：防止内存泄漏，异常安全

### 3. std::vector 的动态调整

```cpp
// 初始分配
std::vector<llama_token> prompt_tokens(prompt.size() + 128);

// 调整大小（缩小到实际使用的大小）
prompt_tokens.resize(n_prompt_tokens);
```

**为什么先大后小？**
- llama_tokenize 需要预先分配足够空间
- token 数量通常小于字符数，但可能超出（中文、特殊字符）
- `+128` 是安全缓冲区
- resize 避免浪费内存

### 4. 指针安全检查

```cpp
if (!initialized_ || !ctx_ || !model_) {
    return "[error: not initialized]";
}
```

**防御性编程**：
- 避免空指针解引用导致崩溃
- 提前返回错误信息
- 用户友好的错误提示

---

## 性能优化技巧

### 1. 合理设置 n_threads

```cpp
// 查询 CPU 核心数
int n_cores = std::thread::hardware_concurrency();
int n_threads = std::max(1, n_cores - 1);  // 留一个核心给系统

inference.loadModel("model.gguf", 2048, n_threads);
```

**经验值**：
- 4 核 CPU：`n_threads = 3-4`
- 8 核 CPU：`n_threads = 6-8`
- 过多线程会导致线程切换开销

### 2. 选择合适的量化级别

```
速度与质量权衡：

Q2_K: 最快，质量最差（不推荐）
Q4_K_M: 平衡之选（推荐）
Q5_K_M: 稍慢，质量更好
Q8_0: 接近原始质量，速度慢
FP16: 最慢，质量最佳
```

### 3. 限制 max_tokens

```cpp
// 避免无限生成
int max_tokens = std::min(user_requested, 512);  // 最多 512 tokens
```

**原因**：
- 超长生成耗时指数增长
- 占用过多服务器资源
- 用户体验反而下降（等待过久）

---

## 应用场景与实践

### 适用场景
1. **单轮问答**：
   ```
   用户: "什么是机器学习？"
   AI: "机器学习是人工智能的一个分支..."
   ```

2. **内容生成**：
   ```
   用户: "写一篇关于春天的诗"
   AI: "春风拂面暖阳照，万物复苏绿意浓..."
   ```

3. **代码补全**：
   ```
   用户: "用Python写一个快速排序"
   AI: "def quicksort(arr):\n    if len(arr) <= 1:..."
   ```

### 不适用场景
1. **多轮对话**：Server-11 不保存历史，每次请求都是独立的
2. **长文本生成**：受 `max_tokens` 限制
3. **高并发**：每次都创建新 context，无资源复用

---

## Server-11 的局限性

### 1. 无对话记忆
```
第1次请求: "我叫Alice"
AI: "你好，Alice！"

第2次请求: "我叫什么名字？"
AI: "抱歉，我不知道。"  ← 忘记了之前的对话
```

**原因**：每次请求都是全新的推理，没有历史记忆。

### 2. 重复创建 Context
每次调用 `generate()` 都使用同一个 `ctx_`，但不保存中间状态：
```cpp
generate("你好");  // ctx_ 被使用
generate("再见");  // ctx_ 重新开始，丢失"你好"的上下文
```

**性能影响**：
- 多轮对话时，每轮都要重新处理完整历史
- 无法利用 KV Cache 加速

### 3. 单线程推理
所有请求共享一个 `SimpleInference` 实例：
```cpp
用户A: "什么是AI？"  → 推理中...
用户B: "天气如何？"  → 等待用户A完成
```

**解决方案预告**：Server-12 引入 SessionManager 解决这些问题。

---

## 与 Server-10 的对比

| 功能 | Server-10 | Server-11 |
|------|----------|----------|
| RESTful API | ✓ | ✓ |
| JSON解析 | ✓ | ✓ |
| 用户注册/登录 | ✓ | ✓ |
| SQLite数据库 | ✓ | ✓ |
| AI推理 | ✗ | ✓（新增）|
| llama.cpp集成 | ✗ | ✓（新增）|
| GGUF模型加载 | ✗ | ✓（新增）|
| 单轮文本生成 | ✗ | ✓（新增）|

**核心升级**：
- 新增 `SimpleInference` 类
- 新增 `/infer-simple` API
- 从纯 Web 服务器 → AI 推理服务器

---

## 总结

### 核心要点
1. **llama.cpp 集成**：在 C++ 服务器中运行大语言模型
2. **SimpleInference 封装**：简化 llama.cpp 复杂 API
3. **单轮推理**：每次请求独立处理，无历史记忆
4. **GGUF 模型**：量化格式，降低资源占用

### 技术栈
- llama.cpp C API
- GGUF 模型格式
- Tokenization 和 Detokenization
- Greedy Sampling 采样策略
- RAII 资源管理
- Lambda 表达式与捕获

### 进阶方向
1. **多轮对话**：Server-12 的 SessionManager
2. **KV Cache 复用**：Server-13-KVcache
3. **批处理**：Server-13-Batch
4. **流式输出**：SSE（Server-Sent Events）
5. **高级采样**：Temperature、Top-K、Top-P

---

**课程设计：** 参考多线程课程文档风格
**适用对象：** 理解 C++ 基础、HTTP 服务器、有一定 AI 背景的开发者
