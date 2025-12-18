# Server-11: llama.cpp集成 + 单轮推理

## 📋 本版本新增功能

相比**Server-10**，本版本添加了：

1. ✅ **SimpleInference类** - 封装llama.cpp推理逻辑
2. ✅ **GGUF模型加载** - 支持量化模型
3. ✅ **单轮文本生成** - 无历史记录的简单推理
4. ✅ **AI推理API** - `/infer-simple`接口
5. ✅ **贪婪采样策略** - 选择最高概率token

---

## 🎯 学习目标

- 理解llama.cpp库的基本使用
- 掌握GGUF格式模型加载
- 学会tokenization和detokenization
- 理解LLM推理的基本流程（prompt处理 → token生成）
- 掌握单轮文本生成的实现

---

## 📁 文件清单

```
server-11-LLM/
├── SimpleInference.h   # ★ 新增：推理引擎类（~250行）
├── Router.h            # ★ 更新：添加/infer-simple接口
├── HttpServer.h        # ★ 更新：添加setupRESTfulRoutes()方法
├── main.cpp            # ★ 更新：添加模型加载逻辑
├── HttpRequest.h       # 继承自Server-10
├── HttpResponse.h      # 继承自Server-10
├── Database.h          # 继承自Server-10
├── Logger.h            # 继承自Server-10
├── ThreadPool.h        # 继承自Server-10
├── test_api.sh         # ★ 新增：测试脚本（包含AI推理测试）
└── README.md           # 本文件
```

---

## 🚀 编译和运行

### 前置依赖

1. **llama.cpp库**：
   ```bash
   # 克隆llama.cpp仓库
   git clone https://github.com/ggerganov/llama.cpp
   cd llama.cpp
   make

   # 或使用CMake编译
   mkdir build && cd build
   cmake ..
   make
   ```

2. **下载GGUF模型**（推荐TinyLlama）：
   ```bash
   # 创建models目录
   mkdir -p ../models

   # 下载TinyLlama-Q4量化模型（约600MB）
   wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
     -O ../models/tinyllama-q4.gguf
   ```

### 编译

```bash
# 假设llama.cpp在上级目录
g++ main.cpp -o server11 \
  -I../llama.cpp \
  -L../llama.cpp/build \
  -lllama \
  -lsqlite3 \
  -lpthread \
  -std=c++11
```

### 运行

```bash
# 方式1: 使用默认路径（../models/tinyllama-q4.gguf）
./server11

# 方式2: 自定义模型路径
export MODEL_PATH=/path/to/your/model.gguf
./server11

# 方式3: 指定端口
./server11 8080
```

**启动示例输出**：
```
=== Server-11: llama.cpp集成 + 单轮推理 ===
Port: 8080

[1/3] Initializing inference engine...
[2/3] Loading model: ../models/tinyllama-q4.gguf
[SimpleInference] Loading model: ../models/tinyllama-q4.gguf
llama_model_loader: loaded meta data with 19 key-value pairs...
[SimpleInference] Model loaded successfully!
[3/3] Starting HTTP server...

📡 AI推理接口（★ Server-11新增）:
  POST /infer-simple       - 单轮推理（JSON格式）
                             Request:  {"prompt":"...", "max_tokens":"64"}
                             Response: {"success":true, "response":"..."}

📋 用户管理接口（继承自Server-10）:
  POST /api/users/register - JSON格式注册
  POST /api/users/login    - JSON格式登录
  GET  /api/users?id=xxx   - 获取用户信息
  POST /api/echo           - JSON回显测试

🌐 传统接口（兼容Server-9）:
  POST /register           - Form格式注册
  POST /login              - Form格式登录
======================================
✅ Server-11 is ready!
======================================
```

---

## 📡 API接口说明

### 新增：AI推理接口

#### POST /infer-simple - 单轮推理

**请求格式**：
```bash
POST /infer-simple
Content-Type: application/json

{
    "prompt": "What is the capital of France?",
    "max_tokens": "64"
}
```

**请求参数**：
- `prompt` (必填): 输入文本提示
- `max_tokens` (可选): 最大生成token数，默认64

**响应格式**：
```json
{
    "success": true,
    "response": "The capital of France is Paris."
}
```

**错误响应**：
```json
{
    "success": false,
    "message": "Prompt required"
}
```

---

## 🧪 完整测试示例

### 测试1: 简单问答

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is 2+2?", "max_tokens":"32"}'
```

**预期输出**：
```json
{"success":true,"response":"2+2 equals 4."}
```

---

### 测试2: 代码生成

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Write a Python function to calculate factorial:", "max_tokens":"128"}'
```

**预期输出**：
```json
{
    "success": true,
    "response": "def factorial(n):\n    if n == 0:\n        return 1\n    return n * factorial(n-1)"
}
```

---

### 测试3: 对话（无上下文）

```bash
# 第一轮
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"My name is Alice.", "max_tokens":"32"}'

# 第二轮（注意：不会记住上一轮的内容）
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is my name?", "max_tokens":"32"}'
```

**第二轮输出**（模型不知道名字是Alice）：
```json
{"success":true,"response":"I don't have information about your name."}
```

⚠️ **重要提示**：Server-11不支持多轮对话，每次请求都是独立的。Server-12将添加会话管理功能。

---

## 💡 核心代码解析

### 1. SimpleInference类架构（SimpleInference.h）

```cpp
class SimpleInference {
public:
    bool loadModel(const std::string& model_path);
    std::string generate(const std::string& prompt, int max_tokens);

private:
    llama_model* model_;

    std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text);
    bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens);
    std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab, int max_tokens);
};
```

---

### 2. 推理流程详解

#### Step 1: 加载模型

```cpp
bool loadModel(const std::string& model_path) {
    llama_model_params model_params = llama_model_default_params();
    model_ = llama_load_model_from_file(model_path.c_str(), model_params);
    return model_ != nullptr;
}
```

#### Step 2: 创建上下文

```cpp
llama_context_params ctx_params = llama_context_default_params();
ctx_params.n_ctx = 2048;       // 上下文窗口大小
ctx_params.n_threads = 4;      // CPU线程数
ctx_params.n_batch = 512;      // batch大小

llama_context* ctx = llama_new_context_with_model(model_, ctx_params);
```

#### Step 3: Tokenize输入

```cpp
std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    std::vector<llama_token> tokens(text.size() * 4);

    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        text.size(),
        tokens.data(),
        tokens.size(),
        true,    // add_special（添加BOS等特殊token）
        false    // parse_special
    );

    if (n_tokens > 0) {
        tokens.resize(n_tokens);
    }

    return tokens;
}
```

**示例**：
```
输入文本: "Hello world"
↓ Tokenize
Token序列: [1, 15043, 3186]  // [BOS, "Hello", "world"]
```

#### Step 4: 处理Prompt（前向传播）

```cpp
bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens) {
    llama_batch batch = llama_batch_init(tokens.size(), 0, 1);

    // 填充batch
    for (size_t i = 0; i < tokens.size(); ++i) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i;
        batch.seq_id[i][0] = 0;
        batch.n_seq_id[i] = 1;
        // 只需要最后一个位置的logits
        batch.logits[i] = (i == tokens.size() - 1) ? 1 : 0;
    }
    batch.n_tokens = tokens.size();

    // 执行前向传播
    int ret = llama_decode(ctx, batch);
    llama_batch_free(batch);

    return ret == 0;
}
```

**原理**：
- 将prompt tokens一次性送入模型
- 只保留最后一个位置的logits（用于下一个token生成）
- KV cache会保存所有位置的键值对

#### Step 5: 生成Tokens（贪婪采样）

```cpp
std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab, int max_tokens) {
    std::string output;
    const int eos_token = llama_vocab_eos(vocab);
    const int vocab_size = llama_vocab_n_tokens(vocab);

    llama_batch gen_batch = llama_batch_init(1, 0, 1);
    int n_past = llama_get_kv_cache_used_cells(ctx);

    for (int step = 0; step < max_tokens; ++step) {
        // 1. 获取logits（每个token的概率分布）
        const float* logits = llama_get_logits(ctx);

        // 2. 贪婪采样：选择概率最大的token
        int next_token = 0;
        float max_prob = logits[0];
        for (int i = 1; i < vocab_size; ++i) {
            if (logits[i] > max_prob) {
                max_prob = logits[i];
                next_token = i;
            }
        }

        // 3. 检查EOS token
        if (next_token == eos_token) break;

        // 4. Token转文本
        char piece[256] = {0};
        int len = llama_token_to_piece(vocab, next_token, piece, sizeof(piece), 0, false);
        if (len > 0) {
            output.append(piece, len);
        }

        // 5. 准备下一次推理
        gen_batch.token[0] = next_token;
        gen_batch.pos[0] = n_past;
        gen_batch.logits[0] = 1;

        llama_decode(ctx, gen_batch);
        n_past++;
    }

    llama_batch_free(gen_batch);
    return output;
}
```

**示例流程**：
```
Prompt: "The capital of France is"

Step 0: 处理prompt → KV cache保存所有位置
        Logits: [0.1, 0.05, 0.8, 0.02, ...]  (32000个值)
        选择: token_1234 ("Paris") (概率最高)
        输出: "Paris"

Step 1: 输入token_1234
        Logits: [0.2, 0.7, 0.05, 0.03, ...]
        选择: token_5678 (".")
        输出: "Paris."

Step 2: 输入token_5678
        Logits: [0.05, 0.9, 0.03, 0.01, ...]
        选择: token_2 (EOS)
        终止生成

最终输出: "Paris."
```

---

### 3. API集成（Router.h）

```cpp
void setupRESTfulRoutes(Database& db, SimpleInference& inference) {
    addRoute("POST", "/infer-simple", [&inference](const HttpRequest& req) {
        auto params = req.parseJson();
        std::string prompt = params["prompt"];
        std::string max_tokens_str = params["max_tokens"];

        if (prompt.empty()) {
            HttpResponse resp;
            resp.setStatusCode(400);
            resp.setBody("{\"success\":false,\"message\":\"Prompt required\"}");
            return resp;
        }

        int max_tokens = 64;
        if (!max_tokens_str.empty()) {
            try {
                max_tokens = std::stoi(max_tokens_str);
            } catch (...) {
                max_tokens = 64;
            }
        }

        // 调用推理
        std::string response = inference.generate(prompt, max_tokens);

        // 返回JSON响应
        HttpResponse resp;
        resp.setStatusCode(200);
        resp.setBody("{\"success\":true,\"response\":\"" + response + "\"}");
        return resp;
    });

    // ... 其他Server-10的API接口 ...
}
```

---

## 🔍 技术细节

### 1. GGUF格式

**GGUF** = GPT-Generated Unified Format

- **优势**：
  - 单文件包含模型+tokenizer+配置
  - 支持量化（Q4_K_M、Q5_K_S等）
  - 加载速度快
  - 内存占用小

- **常见量化级别**：
  - `Q4_K_M`: 4-bit量化，中等精度（推荐）
  - `Q5_K_S`: 5-bit量化，高精度
  - `Q8_0`: 8-bit量化，接近原始精度

---

### 2. Tokenization

**Token**：文本的最小单位（可能是单词、子词或字符）

**示例**：
```
文本: "Hello, world!"
↓
Tokens: ["Hello", ",", " world", "!"]
↓
Token IDs: [15043, 28725, 1526, 28808]
```

**特殊Tokens**：
- `BOS` (Beginning of Sequence): 序列开始标记
- `EOS` (End of Sequence): 序列结束标记

---

### 3. 贪婪采样 vs 其他采样策略

| 策略 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **贪婪采样** (Greedy) | 总是选择概率最高的token | 确定性、快速 | 重复、单调 |
| Top-k | 从概率最高的k个token中随机采样 | 多样性好 | 可能不合理 |
| Top-p (Nucleus) | 累积概率达到p时停止，从中采样 | 平衡质量和多样性 | 计算复杂 |
| 温度采样 | 调整概率分布的"锐度" | 灵活控制 | 需调参 |

**Server-11使用贪婪采样**，因为：
- 实现简单，适合教学
- 确定性输出，便于测试
- 性能最优

---

### 4. KV Cache

**问题**：每次生成新token时，需要重新计算所有历史tokens的attention吗？

**答案**：不需要！使用KV Cache优化。

**原理**：
```
Prompt: "The capital of France"

首次计算:
  Token 0: "The"     → 计算Key0, Value0 → 保存到Cache
  Token 1: "capital" → 计算Key1, Value1 → 保存到Cache
  Token 2: "of"      → 计算Key2, Value2 → 保存到Cache
  Token 3: "France"  → 计算Key3, Value3 → 保存到Cache

生成Token 4: "is"
  只需计算Token 4的Key和Value
  从Cache读取Key0-3, Value0-3
  计算Attention → 生成Token 4

生成Token 5: "Paris"
  只需计算Token 5的Key和Value
  从Cache读取Key0-4, Value0-4
  计算Attention → 生成Token 5
```

**优势**：
- 避免重复计算
- 推理速度大幅提升
- 内存换时间

---

## 📚 与Server-10的对比

| 特性 | Server-10 | Server-11 |
|------|-----------|-----------|
| JSON解析 | ✅ | ✅ |
| RESTful API | ✅ | ✅ |
| 查询参数 | ✅ | ✅ |
| AI推理 | ❌ | ✅ |
| 模型加载 | ❌ | ✅ GGUF |
| 文本生成 | ❌ | ✅ 单轮 |
| 多轮对话 | ❌ | ❌ (Server-12实现) |
| 依赖库 | SQLite | SQLite + llama.cpp |
| 代码量 | ~400行 | ~650行 (+250行推理逻辑) |

---

## 🎓 学习检查清单

完成以下任务后，你已经掌握了本节内容：

- [ ] 理解llama.cpp库的作用和使用方法
- [ ] 成功编译并运行Server-11
- [ ] 理解GGUF格式和量化的概念
- [ ] 掌握tokenization和detokenization流程
- [ ] 理解LLM推理的完整流程（加载→tokenize→前向传播→生成）
- [ ] 理解贪婪采样策略的原理
- [ ] 理解KV Cache的优化原理
- [ ] 成功测试/infer-simple接口
- [ ] 理解为什么Server-11不支持多轮对话
- [ ] 能够解释单轮推理的局限性

---

## 🐛 常见问题

### Q1: 编译时找不到llama.h？

**A**: 确保在编译时指定了正确的include路径：
```bash
g++ main.cpp -o server11 -I/path/to/llama.cpp -L/path/to/llama.cpp/build -lllama -lsqlite3 -lpthread -std=c++11
```

---

### Q2: 运行时提示"Failed to load model"？

**A**: 检查模型文件是否存在：
```bash
ls -lh ../models/tinyllama-q4.gguf

# 或设置MODEL_PATH环境变量
export MODEL_PATH=/absolute/path/to/model.gguf
```

---

### Q3: 生成的文本质量不好？

**A**: 可能的原因：
1. **模型太小**：TinyLlama只有1.1B参数，能力有限
2. **Prompt不清晰**：尝试更详细的prompt
3. **max_tokens太小**：增加到128或256
4. **贪婪采样限制**：考虑在Server-12中实现温度采样

---

### Q4: Server-11支持多轮对话吗？

**A**: 不支持。每次请求都是独立的，模型不会记住之前的对话。多轮对话需要：
1. 会话管理（session management）
2. 对话历史存储
3. 上下文拼接

这些功能将在**Server-12**中实现。

---

### Q5: 为什么推理这么慢？

**A**: 可能的原因：
1. **CPU推理**：Server-11使用CPU，速度较慢。GPU推理需要CUDA支持。
2. **模型大小**：即使是量化模型，推理仍需计算。
3. **线程数不足**：增加`ctx_params.n_threads`值。

**优化建议**：
```cpp
ctx_params.n_threads = 8;  // 增加线程数
ctx_params.n_batch = 1024; // 增加batch大小
```

---

### Q6: llama_decode()返回非零值？

**A**: 常见原因：
1. **Context满了**：减少max_tokens或增加n_ctx
2. **Batch配置错误**：检查batch.n_tokens和batch.logits设置
3. **模型损坏**：重新下载模型文件

---

## 🚀 下一步

学完Server-11后，继续学习：

**Server-12**: 会话管理 + 多轮对话
- Session管理
- 对话历史存储
- 上下文拼接策略
- KV Cache复用
- 实现真正的聊天机器人

**预告**：Server-12将实现：
```bash
# 第一轮
POST /chat {"session_id":"abc123", "message":"My name is Alice"}
→ "Nice to meet you, Alice!"

# 第二轮（记住了名字）
POST /chat {"session_id":"abc123", "message":"What is my name?"}
→ "Your name is Alice."
```

---

**编写日期**: 2025-12-11
**作者**: Claude Code
**版本**: Server-11
**下一版本**: Server-12 (会话管理 + 多轮对话)
