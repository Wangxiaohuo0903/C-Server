# Server-11：大模型结合服务器初探
## 第三部分：llama.cpp推理引擎

> **学习目标**：理解为什么选择llama.cpp、它的架构设计、核心API使用、以及Server-11的SimpleInference.h是如何封装它的。

---

## 1. 为什么选择llama.cpp？

### 1.1 推理框架的选择困境

当我们决定在Server-11中集成大模型时，面临一个关键问题：**用什么来运行模型？**

**市场上的主流方案**：

#### 方案1：PyTorch + Transformers

```python
# 需要Python环境
from transformers import AutoModelForCausalLM, AutoTokenizer

model = AutoModelForCausalLM.from_pretrained("TinyLlama-1.1B")
tokenizer = AutoTokenizer.from_pretrained("TinyLlama-1.1B")

inputs = tokenizer("What is AI?", return_tensors="pt")
outputs = model.generate(**inputs, max_length=100)
text = tokenizer.decode(outputs[0])
```

**优点**：
- ✅ 功能最强大（训练+推理）
- ✅ 生态最好（HuggingFace、各种工具）
- ✅ 文档丰富
- ✅ 社区活跃

**缺点**：
- ❌ 需要Python环境（Server-11是C++）
- ❌ 依赖巨大：
  ```
  pip install transformers
  → 需要: torch(2GB), numpy, scipy, ...
  → 总计: ~3GB下载
  ```
- ❌ 内存占用大（未量化）
- ❌ 部署复杂（需要管理Python依赖）
- ❌ 推理速度慢（没有针对CPU优化）

#### 方案2：TensorFlow Lite

```cpp
// C++ API
#include "tensorflow/lite/interpreter.h"

std::unique_ptr<tflite::FlatBufferModel> model =
    tflite::FlatBufferModel::BuildFromFile("model.tflite");

tflite::InterpreterBuilder builder(*model, resolver);
builder(&interpreter);

interpreter->AllocateTensors();
// ... 推理代码
```

**优点**：
- ✅ 有C++ API
- ✅ 针对移动端优化

**缺点**：
- ❌ 不支持大模型（主要是小型CNN、RNN）
- ❌ 转换复杂（PyTorch → TFLite很困难）
- ❌ 量化支持有限
- ❌ 对Transformer支持不好

#### 方案3：ONNX Runtime

```cpp
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

Ort::Env env;
Ort::SessionOptions session_options;
Ort::Session session(env, "model.onnx", session_options);

// 推理代码
auto output_tensors = session.Run(run_options, input_names, input_tensors, ...);
```

**优点**：
- ✅ 跨框架（支持PyTorch、TensorFlow）
- ✅ 有优化工具
- ✅ 支持多种硬件

**缺点**：
- ❌ 大模型转换复杂：
  ```bash
  # PyTorch → ONNX经常失败
  torch.onnx.export(model, ...)
  # 错误: 不支持dynamic控制流
  # 错误: 不支持某些自定义操作
  ```
- ❌ 量化支持有限（主要是INT8）
- ❌ 对CPU推理优化不够

#### 方案4：llama.cpp ⭐ Server-11的选择

```cpp
#include "llama.h"

llama_model* model = llama_load_model_from_file("tinyllama-q4.gguf");
llama_context* ctx = llama_new_context_with_model(model, params);

std::vector<llama_token> tokens = tokenize(prompt);
llama_decode(ctx, batch);
std::string output = generate();
```

**优点**：
- ✅ **纯C/C++**：无需Python，直接集成
- ✅ **零依赖**：只需C++标准库
- ✅ **CPU优化**：SIMD指令、多线程
- ✅ **量化支持**：Q2/Q4/Q5/Q8原生支持
- ✅ **GGUF格式**：专为推理设计
- ✅ **跨平台**：Windows/Linux/macOS/iOS/Android
- ✅ **单文件部署**：一个.so/.dll搞定
- ✅ **内存高效**：mmap加载、KV cache优化

**缺点**：
- ❌ 只支持推理（不支持训练）
- ❌ 只支持Llama架构（但已覆盖大部分主流模型）
- ❌ 生态相对小（但快速发展中）

### 1.2 Server-11的需求分析

**我们的具体需求**：

```
✅ C++集成          → llama.cpp完美匹配
✅ 简单部署          → 单个.so文件
✅ CPU推理          → llama.cpp有SIMD优化
✅ 量化模型          → Q4_K支持
✅ 内存受限(4-8GB)   → mmap + 量化
✅ 推理速度可接受     → 2-3 tokens/s足够
❌ 不需要训练        → 用预训练模型
❌ 不需要GPU         → CPU就够了
```

**对比结果**：

| 需求 | PyTorch | TFLite | ONNX | llama.cpp |
|------|---------|--------|------|-----------|
| C++集成 | ❌ | ✅ | ✅ | ✅ |
| 零依赖 | ❌ | ❌ | ❌ | ✅ |
| CPU优化 | ❌ | ⚠️ | ⚠️ | ✅ |
| 量化Q4 | ❌ | ❌ | ❌ | ✅ |
| 简单部署 | ❌ | ⚠️ | ⚠️ | ✅ |
| **总分** | 1/5 | 2/5 | 2/5 | **5/5** |

**结论**：llama.cpp是Server-11的最佳选择！

---

## 2. llama.cpp架构详解

### 2.1 整体架构

```
llama.cpp项目结构
├── llama.h/cpp          # 高层API（我们主要用这个）
│   ├── llama_load_model_from_file()
│   ├── llama_new_context_with_model()
│   ├── llama_tokenize()
│   ├── llama_decode()
│   └── llama_get_logits()
│
├── ggml.h/cpp           # 底层张量运算库
│   ├── 矩阵乘法
│   ├── 卷积运算
│   ├── 激活函数
│   └── SIMD优化
│
├── common/              # 通用工具
│   ├── sampling.h       # 采样策略
│   ├── grammar.h        # 语法约束
│   └── log.h            # 日志工具
│
├── examples/            # 示例程序
│   ├── main/            # 命令行聊天
│   ├── server/          # HTTP API服务器
│   ├── simple/          # 简单示例
│   └── ...
│
└── CMakeLists.txt       # 构建配置
```

### 2.2 核心组件

#### 组件1：llama.h/cpp（高层API）

**作用**：提供易用的推理接口

```cpp
// 主要数据结构
struct llama_model;    // 模型（参数）
struct llama_context;  // 推理上下文（状态）
struct llama_batch;    // 批处理输入

// 主要函数
llama_model* llama_load_model_from_file(const char* path, ...);
llama_context* llama_new_context_with_model(llama_model*, ...);
int llama_tokenize(const llama_vocab*, const char* text, ...);
int llama_decode(llama_context*, llama_batch);
float* llama_get_logits(llama_context*);
void llama_free(llama_context*);
void llama_free_model(llama_model*);
```

**类比**：就像数据库的高层API

```cpp
// 数据库高层API
sqlite3* db = sqlite3_open("users.db");
sqlite3_exec(db, "SELECT * FROM users", ...);
sqlite3_close(db);

// llama.cpp高层API
llama_model* model = llama_load_model_from_file("model.gguf");
llama_decode(ctx, batch);
llama_free_model(model);
```

#### 组件2：ggml.h/cpp（底层引擎）

**作用**：高性能的张量运算

```cpp
// GGML的核心：张量（Tensor）
struct ggml_tensor {
    enum ggml_type type;     // 数据类型（FP32, Q4_K等）
    int64_t ne[4];           // 维度（n_elements）
    size_t nb[4];            // 步长（n_bytes）
    void* data;              // 数据指针
    // ...
};

// 基本运算
ggml_tensor* ggml_mul_mat(ctx, a, b);        // 矩阵乘法
ggml_tensor* ggml_add(ctx, a, b);            // 加法
ggml_tensor* ggml_norm(ctx, a);              // 归一化
ggml_tensor* ggml_rope(ctx, a, n_past, ...); // RoPE位置编码
```

**SIMD优化示例**：

```cpp
// 朴素实现（慢）
void vec_mul_scalar(float* dst, const float* src, float scalar, int n) {
    for (int i = 0; i < n; ++i) {
        dst[i] = src[i] * scalar;
    }
}
// 速度：处理1000个元素 ≈ 1000次循环

// SIMD优化（快）
void vec_mul_scalar_simd(float* dst, const float* src, float scalar, int n) {
    __m256 scalar_vec = _mm256_set1_ps(scalar);  // 广播scalar到8个位置
    for (int i = 0; i < n; i += 8) {
        __m256 src_vec = _mm256_loadu_ps(src + i);   // 一次加载8个float
        __m256 result = _mm256_mul_ps(src_vec, scalar_vec);  // 一次乘8个
        _mm256_storeu_ps(dst + i, result);           // 一次存8个
    }
}
// 速度：处理1000个元素 ≈ 125次循环（8倍加速）
```

**为什么GGML而不是Eigen、Armadillo？**

| 库 | 特点 | 缺点 |
|------|------|------|
| Eigen | 通用矩阵库，模板优化 | 不支持量化，编译慢 |
| Armadillo | 类似MATLAB语法 | 不支持GPU，量化差 |
| **GGML** | 专为LLM设计 | ✅ 原生量化支持<br>✅ CPU+GPU<br>✅ 超快 |

#### 组件3：common/（通用工具）

**sampling.h - 采样策略**：

```cpp
// 贪婪采样
int llama_sample_token_greedy(llama_context* ctx, ...);

// Top-K采样
int llama_sample_token_top_k(llama_context* ctx, int k, ...);

// Top-P采样
int llama_sample_token_top_p(llama_context* ctx, float p, ...);

// Temperature采样
void llama_sample_temperature(llama_context* ctx, float temp, ...);
```

**grammar.h - 语法约束**：

```cpp
// 强制生成JSON格式
const char* json_grammar =
    "root ::= object\n"
    "object ::= '{' pair (',' pair)* '}'\n"
    "pair ::= string ':' value\n"
    "...";

llama_grammar* grammar = llama_grammar_init(json_grammar);
llama_sample_grammar(ctx, grammar);  // 只生成符合JSON的token
```

### 2.3 编译产物

**编译llama.cpp**：

```bash
cd third_party/llama.cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j4

# 编译时间：约2-3分钟
# 生成的文件：
```

**生成的库文件**：

```
build/bin/
├── libllama.so        # llama.cpp核心库（~15MB）
│   └── 包含：
│       ├── 模型加载
│       ├── Tokenization
│       ├── Transformer前向传播
│       └── 采样逻辑
│
├── libggml.so         # GGML张量库（~8MB）
│   └── 包含：
│       ├── 矩阵运算
│       ├── SIMD优化
│       └── 量化/反量化
│
├── llama-cli          # 命令行工具
│   └── 示例：
│       ./llama-cli -m model.gguf -p "Hello"
│
└── llama-server       # HTTP服务器
    └── 示例：
        ./llama-server -m model.gguf --port 8080
```

**Server-11使用**：

```cpp
// 只需要链接两个库
g++ main.cpp -o server11 \
    -L/app/third_party/llama.cpp/build/bin \
    -lllama \    // libllama.so
    -lggml \     // libggml.so
    -lpthread \
    -std=c++11

// 运行时需要设置库路径
export LD_LIBRARY_PATH=/app/third_party/llama.cpp/build/bin:$LD_LIBRARY_PATH
./server11
```

---

## 3. 核心API详解

### 3.1 模型加载

#### API: llama_load_model_from_file

**函数签名**：

```cpp
LLAMA_API llama_model* llama_load_model_from_file(
    const char* path_model,
    llama_model_params params
);
```

**参数详解**：

```cpp
struct llama_model_params {
    int32_t n_gpu_layers;     // GPU层数（0=全CPU）
    bool vocab_only;          // 只加载词汇表（不加载参数）
    bool use_mmap;            // 使用mmap（默认true）
    bool use_mlock;           // 锁定内存（防止swap）
    // ...
};

// 默认参数
llama_model_params params = llama_model_default_params();
params.n_gpu_layers = 0;      // Server-11用CPU
params.use_mmap = true;       // 启用mmap优化
params.use_mlock = false;     // 不锁定内存（避免占用太多）
```

**使用示例**：

```cpp
// SimpleInference.h:33-53
bool loadModel(const std::string& model_path) {
    // 设置参数
    llama_model_params model_params = llama_model_default_params();

    // 加载模型
    std::cout << "[SimpleInference] Loading model: " << model_path << "\n";
    model_ = llama_load_model_from_file(model_path.c_str(), model_params);

    if (!model_) {
        std::cerr << "[SimpleInference] Failed to load model!\n";
        return false;
    }

    // 打印模型信息（llama.cpp内部会输出）
    // llama_model_loader: loaded meta data with 37 key-value pairs
    // print_info: model type = 3B
    // print_info: model params = 361.82 M

    std::cout << "[SimpleInference] Model loaded successfully!\n";
    return true;
}
```

**内部过程**：

```
llama_load_model_from_file内部：

1. 打开GGUF文件
   fopen("smollm-360m-q4.gguf", "rb")

2. 读取文件头
   magic = 0x46554747 ('GGUF')
   version = 3
   n_tensors = 290
   n_kv = 37

3. 读取元数据
   for (int i = 0; i < 37; ++i) {
       key = read_string();
       value = read_value();
       metadata[key] = value;
   }

   示例：
   general.name = "SmolLM 360M"
   llama.context_length = 2048
   tokenizer.ggml.tokens = [49152个词]

4. 分配内存/mmap映射
   if (use_mmap) {
       data = mmap(fd, size);  // 内存映射
   } else {
       data = malloc(size);     // 分配内存
       read(fd, data, size);   // 读取文件
   }

5. 解析Tensor布局
   for (int i = 0; i < 290; ++i) {
       name = read_string();
       type = read_type();
       shape = read_shape();
       offset = read_offset();

       tensors[name] = {
           .data = mmap_base + offset,
           .type = type,
           .shape = shape
       };
   }

6. 初始化词汇表
   从metadata中提取token列表
   构建ID→文本的映射

7. 返回模型指针
   return &model;
```

**mmap的优势**：

```
传统方式（use_mmap=false）：
1. malloc(259MB)        → 分配内存
2. read(file, buf, 259MB) → 读取文件
3. 峰值内存：259MB × 2 = 518MB

mmap方式（use_mmap=true）：
1. mmap(file, 259MB)    → 映射到虚拟内存
2. 按需加载（page fault触发）
3. 峰值内存：~300MB（只加载使用的部分）

加载时间：
传统：~3-5秒（读取整个文件）
mmap：<1秒（只映射，不读取）
```

### 3.2 创建推理上下文

#### API: llama_new_context_with_model

**函数签名**：

```cpp
LLAMA_API llama_context* llama_new_context_with_model(
    llama_model* model,
    llama_context_params params
);
```

**参数详解**：

```cpp
struct llama_context_params {
    uint32_t n_ctx;           // 上下文窗口大小
    uint32_t n_batch;         // 批处理大小
    uint32_t n_threads;       // CPU线程数

    bool logits_all;          // 是否返回所有位置的logits
    bool embeddings;          // 嵌入模式

    // ...更多参数
};

// Server-11的配置
llama_context_params ctx_params = llama_context_default_params();
ctx_params.n_ctx = 2048;      // 上下文窗口：最多2048 tokens
ctx_params.n_threads = 4;      // CPU线程数：4核心
ctx_params.n_batch = 512;      // 批处理大小
```

**使用示例**：

```cpp
// SimpleInference.h:68-76
std::string generate(const std::string& prompt, int max_tokens = 64) {
    // 每次generate都创建新的上下文（无历史记录）
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_threads = 4;
    ctx_params.n_batch = 512;

    llama_context* ctx = llama_new_context_with_model(model_, ctx_params);
    if (!ctx) {
        return "[ERROR] Failed to create context";
    }

    // ... 推理逻辑 ...

    llama_free(ctx);  // 释放上下文
    return output;
}
```

**内部过程**：

```
llama_new_context_with_model内部：

1. 分配KV Cache
   每层的K和V：
   kv_cache_size = n_ctx × n_embd × sizeof(float16) × 2
                 = 2048 × 960 × 2 × 2
                 = 7.9MB/层

   总计：7.9MB × 32层 = 253MB

2. 分配计算缓冲区
   用于存储中间激活值
   大小：约50-100MB

3. 创建计算图
   构建Transformer的计算流程
   （前向传播的蓝图）

4. 设置线程池
   if (n_threads > 1) {
       create_thread_pool(n_threads);
   }

5. 返回上下文指针
   return &ctx;
```

**n_ctx的影响**：

```
n_ctx = 512:
├── KV Cache: 253MB × (512/2048) = 63MB
├── 可处理：最多512 tokens
└── 适用：短对话、问答

n_ctx = 2048:
├── KV Cache: 253MB
├── 可处理：最多2048 tokens
└── 适用：中等长度文本

n_ctx = 4096:
├── KV Cache: 253MB × 2 = 506MB
├── 可处理：最多4096 tokens
└── 适用：长文档分析
```

### 3.3 Tokenization

#### API: llama_tokenize

**函数签名**：

```cpp
LLAMA_API int llama_tokenize(
    const llama_vocab* vocab,
    const char* text,
    int32_t text_len,
    llama_token* tokens,
    int32_t n_tokens_max,
    bool add_special,
    bool parse_special
);
```

**参数说明**：

```cpp
vocab         // 词汇表（从模型获取）
text          // 输入文本
text_len      // 文本长度
tokens        // 输出buffer（存放token IDs）
n_tokens_max  // buffer最大容量
add_special   // 是否添加BOS（开始标记）
parse_special // 是否解析特殊token

返回值：
  > 0: 成功，返回token数量
  < 0: buffer太小，返回需要的大小（负数）
```

**使用示例**：

```cpp
// SimpleInference.h:110-144
std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text) {
    // 预分配空间（文本长度×4通常够用）
    std::vector<llama_token> tokens(text.size() * 4);

    // 调用tokenize
    int n_tokens = llama_tokenize(
        vocab,
        text.c_str(),
        text.size(),
        tokens.data(),
        tokens.size(),
        true,    // add_special: 添加BOS
        false    // parse_special: 不解析特殊token
    );

    if (n_tokens < 0) {
        // buffer不够，重新分配
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(
            vocab,
            text.c_str(),
            text.size(),
            tokens.data(),
            tokens.size(),
            true,
            false
        );
    }

    tokens.resize(n_tokens);
    return tokens;
}
```

**实际例子**：

```cpp
const llama_vocab* vocab = llama_model_get_vocab(model);

// 例子1：简单文本
std::string text1 = "Hello";
auto tokens1 = tokenize(vocab, text1);
// 结果：[1, 15496]
//       BOS "Hello"

// 例子2：数学表达式
std::string text2 = "2+2=4";
auto tokens2 = tokenize(vocab, text2);
// 结果：[1, 362, 10, 362, 28, 19]
//       BOS 2   +   2   =   4

// 例子3：中文
std::string text3 = "你好世界";
auto tokens3 = tokenize(vocab, text3);
// 结果：[1, 57668, 57902, 32620, 32566]
//       BOS 你    好    世    界
// 注意：中文通常每个字是1-2个token
```

### 3.4 推理执行

#### API: llama_decode

**函数签名**：

```cpp
LLAMA_API int llama_decode(
    llama_context* ctx,
    llama_batch batch
);
```

**llama_batch结构**：

```cpp
struct llama_batch {
    int32_t n_tokens;           // batch中的token数量

    llama_token* token;         // token ID数组
    llama_pos* pos;             // 位置索引数组
    int32_t* n_seq_id;          // 序列数量数组
    llama_seq_id** seq_id;      // 序列ID数组
    int8_t* logits;             // 是否需要logits
};

// 创建batch
llama_batch batch = llama_batch_init(
    n_tokens,   // 最大token数
    0,          // embd（不使用）
    1           // 最大序列数
);
```

**使用示例1：处理Prompt**

```cpp
// SimpleInference.h:150-169
bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens) {
    // 创建batch
    llama_batch batch = llama_batch_init(tokens.size(), 0, 1);

    // 填充batch
    for (size_t i = 0; i < tokens.size(); ++i) {
        batch.token[i] = tokens[i];     // Token ID
        batch.pos[i] = i;                // 位置（0, 1, 2, ...）
        batch.seq_id[i][0] = 0;          // 序列0
        batch.n_seq_id[i] = 1;           // 1个序列

        // 只需最后一个位置的logits
        batch.logits[i] = (i == tokens.size() - 1) ? 1 : 0;
    }
    batch.n_tokens = tokens.size();

    // 执行前向传播
    int ret = llama_decode(ctx, batch);

    llama_batch_free(batch);
    return ret == 0;  // 0表示成功
}
```

**使用示例2：生成下一个Token**

```cpp
// SimpleInference.h:222-233
// 准备batch（只有1个token）
llama_batch gen_batch = llama_batch_init(1, 0, 1);

gen_batch.n_tokens = 1;
gen_batch.token[0] = next_token;      // 刚生成的token
gen_batch.pos[0] = n_past;            // 当前位置
gen_batch.seq_id[0][0] = 0;
gen_batch.n_seq_id[0] = 1;
gen_batch.logits[0] = 1;              // 需要logits

// 执行推理
if (llama_decode(ctx, gen_batch) != 0) {
    std::cerr << "Decode failed\n";
    break;
}

llama_batch_free(gen_batch);
n_past++;  // 位置+1
```

**内部过程**：

```
llama_decode内部：

1. 嵌入层
   for (int i = 0; i < batch.n_tokens; ++i) {
       token_id = batch.token[i];
       embedding = token_embd.weight[token_id];
       // 取出960维向量
   }

2. Transformer层（循环32层）
   for (int layer = 0; layer < 32; ++layer) {
       // 2.1 自注意力
       Q = input × W_q;
       K = input × W_k;
       V = input × W_v;

       // 从KV Cache读取历史
       K_cache = kv_cache[layer].k[0:n_past];
       V_cache = kv_cache[layer].v[0:n_past];

       // 拼接
       K_full = concat(K_cache, K);
       V_full = concat(V_cache, V);

       // 注意力计算
       scores = Q × K_full^T / sqrt(d_k);
       attn = softmax(scores);
       output = attn × V_full;

       // 保存到Cache
       kv_cache[layer].k[n_past] = K;
       kv_cache[layer].v[n_past] = V;

       // 2.2 前馈网络
       gate = input × W_gate;
       up = input × W_up;
       ffn = (gate * silu(up)) × W_down;

       // 2.3 残差连接
       input = input + attn + ffn;
   }

3. 输出层（如果需要logits）
   if (batch.logits[i]) {
       logits = output × lm_head.weight;
       // 49152个分数
   }

4. 返回
   return 0;  // 成功
```

### 3.5 获取输出

#### API: llama_get_logits

**函数签名**：

```cpp
LLAMA_API float* llama_get_logits(llama_context* ctx);
```

**使用示例**：

```cpp
// SimpleInference.h:191-206
// 获取logits
const float* logits = llama_get_logits(ctx);
if (!logits) {
    std::cerr << "Failed to get logits\n";
    break;
}

// 贪婪采样：找最大值
int next_token = 0;
float max_prob = logits[0];

for (int i = 1; i < vocab_size; ++i) {
    if (logits[i] > max_prob) {
        max_prob = logits[i];
        next_token = i;
    }
}

std::cout << "Selected token: " << next_token
          << " (prob: " << max_prob << ")\n";
```

**logits数组**：

```
返回的logits是一个浮点数组：
float logits[49152];  // SmolLM的词汇表大小

logits[0] = -5.23      // Token 0的分数
logits[1] = -3.45      // Token 1的分数
logits[2] = -4.67      // Token 2的分数
...
logits[10840] = 8.92   // Token 10840的分数（最高）
...
logits[49151] = -6.78  // 最后一个token

注意：
- logits是原始分数（未归一化）
- 值域：[-∞, +∞]
- 需要softmax转换为概率：
  probs[i] = exp(logits[i]) / Σ exp(logits[j])
```

#### API: llama_token_to_piece

**函数签名**：

```cpp
LLAMA_API int llama_token_to_piece(
    const llama_vocab* vocab,
    llama_token token,
    char* buf,
    int32_t length,
    int32_t lstrip,
    bool special
);
```

**使用示例**：

```cpp
// SimpleInference.h:215-219
char piece[256];
int len = llama_token_to_piece(
    vocab,
    next_token,      // Token ID
    piece,           // 输出buffer
    sizeof(piece),   // buffer大小
    0,               // lstrip: 不移除前导空格
    false            // special: 不特殊处理
);

if (len > 0) {
    output.append(piece, len);
    std::cout << piece;  // 实时输出
}
```

**实际例子**：

```cpp
// Token → 文本
llama_token_to_piece(vocab, 15496, buf, 256, 0, false);
// 结果：buf = "Hello", len = 5

llama_token_to_piece(vocab, 10840, buf, 256, 0, false);
// 结果：buf = " artificial", len = 11（注意前导空格）

llama_token_to_piece(vocab, 0, buf, 256, 0, false);
// 结果：buf = "<|endoftext|>", len = 13
```

---

## 4. API版本兼容性问题

### 4.1 Server-11遇到的问题

**错误信息**：

```
SimpleInference.h:184:22: error: 'llama_get_kv_cache_used_cells'
was not declared in this scope
     int n_past = llama_get_kv_cache_used_cells(ctx);
                  ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

**原因分析**：

```
Server-11的SimpleInference.h是基于旧版llama.cpp编写的
但我们克隆的是最新版llama.cpp（2024-12-17）

API变更历史：
├── v0.1-v0.7 (2023-2024)
│   └── 稳定期，API基本不变
│       ├── llama_get_kv_cache_used_cells() ✅ 存在
│       └── llama_free_model() ✅ 存在
│
├── v0.8 (2024-11)
│   └── 重构KV Cache机制
│       ├── llama_get_kv_cache_used_cells() ❌ 移除
│       └── 新增llama_kv_cache_seq_pos_max() ✅
│
└── v0.9 (2024-12)
    └── API重命名
        ├── llama_free_model() → llama_model_free()
        └── llama_free() → llama_context_free()（但兼容别名仍存在）
```

### 4.2 解决方案

**方案1：锁定兼容版本（Server-11采用）**

```bash
# 步骤1：找到AI-infra使用的版本
cd ../AI-infra/third_party/llama.cpp
git log -1

# 输出：
# commit ece0f5c7...
# Date:   Tue Oct 15 10:23:45 2024 +0800
# Compatible with SimpleInference.h

# 步骤2：复制到Server-11
cd ../../server2025/server-11-LLM
rm -rf third_party/llama.cpp
cp -r ../../AI-infra/third_party/llama.cpp ./third_party/

# 步骤3：验证
cd third_party/llama.cpp
git log -1
# 确认是commit ece0f5c

# 步骤4：记录版本信息
echo "ece0f5c" > VERSION.txt
echo "Compatible with SimpleInference.h (2024-10-15)" >> VERSION.txt
```

**方案2：修改代码适配新API**

```cpp
// 旧代码（SimpleInference.h原始版本）
int n_past = llama_get_kv_cache_used_cells(ctx);  // ❌ v0.8+不存在

// 修改方案1：手动跟踪（Server-11采用）
std::string generate_tokens(..., int n_past_init) {
    int n_past = n_past_init;  // 从prompt长度开始

    for (int step = 0; step < max_tokens; ++step) {
        // 生成一个token
        ...
        n_past++;  // 手动递增
    }
}

// 修改方案2：使用新API（需要llama.cpp v0.8+）
int n_past = llama_kv_cache_seq_pos_max(ctx, 0);  // 序列0的最大位置
```

**Server-11的最终方案**：

```cpp
// SimpleInference.h:179
std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab,
                           int max_tokens, int n_past_init) {
    // ↑ 新增参数n_past_init，从外部传入

    int n_past = n_past_init;  // 从prompt长度开始

    for (int step = 0; step < max_tokens; ++step) {
        // ...生成逻辑...
        n_past++;  // 每生成一个token，位置+1
    }
}

// 调用处（SimpleInference.h:96）
std::string output = generate_tokens(ctx, vocab, max_tokens, tokens.size());
//                                                            ↑ 传入prompt长度
```

### 4.3 版本管理最佳实践

**创建版本文件**：

```bash
# third_party/llama.cpp/VERSION.txt
commit: ece0f5c7abc123def456
date: 2024-10-15
compatible_with: SimpleInference.h
api_version: v0.7
notes: Stable version before KV cache refactor
```

**创建变更日志**：

```markdown
# third_party/llama.cpp/CHANGES.md

## Server-11使用的llama.cpp版本

### 版本信息
- Commit: ece0f5c
- Date: 2024-10-15
- API Version: v0.7

### 为什么不用最新版？
最新版（2024-12-17, commit 2973a65）有以下API变更：
1. `llama_get_kv_cache_used_cells()` 被移除
2. `llama_free_model()` → `llama_model_free()`

SimpleInference.h基于v0.7 API编写，需要兼容版本。

### 升级计划
将来可以升级到最新版，需要修改：
1. 移除`llama_get_kv_cache_used_cells()`调用
2. 改用手动跟踪n_past（已实现）
3. 测试验证
```

---

## 5. SimpleInference.h封装

### 5.1 设计理念

**为什么要封装？**

```
直接使用llama.cpp API（复杂）：
int main() {
    llama_backend_init();
    llama_model_params params = llama_model_default_params();
    llama_model* model = llama_load_model_from_file(...);
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_threads = 4;
    llama_context* ctx = llama_new_context_with_model(model, ctx_params);
    const llama_vocab* vocab = llama_model_get_vocab(model);
    std::vector<llama_token> tokens(text.size() * 4);
    int n_tokens = llama_tokenize(vocab, text.c_str(), ...);
    llama_batch batch = llama_batch_init(...);
    // ... 还有几十行代码 ...
}

使用SimpleInference（简单）：
int main() {
    SimpleInference inference;
    inference.loadModel("model.gguf");
    std::string result = inference.generate("What is AI?", 64);
    std::cout << result << "\n";
}
```

**封装的好处**：
- ✅ 隐藏复杂性（用户不需要了解llama.cpp细节）
- ✅ 易于使用（3行代码搞定）
- ✅ 资源管理（RAII自动释放）
- ✅ 错误处理（统一的错误检查）

### 5.2 类设计

```cpp
// SimpleInference.h:18-103
class SimpleInference {
public:
    SimpleInference() : model_(nullptr) {}

    ~SimpleInference() {
        if (model_) {
            llama_free_model(model_);  // RAII：自动释放
        }
    }

    // 公开接口
    bool loadModel(const std::string& model_path);
    std::string generate(const std::string& prompt, int max_tokens = 64);

private:
    llama_model* model_;  // 模型指针

    // 内部辅助函数
    std::vector<llama_token> tokenize(const llama_vocab* vocab, const std::string& text);
    bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens);
    std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab,
                                int max_tokens, int n_past_init);
};
```

**设计模式**：

1. **RAII（Resource Acquisition Is Initialization）**
```cpp
~SimpleInference() {
    if (model_) {
        llama_free_model(model_);  // 析构时自动释放
    }
}

// 使用：
{
    SimpleInference inference;
    inference.loadModel("model.gguf");
    // ...
}  // 离开作用域，自动调用析构函数释放模型
```

2. **接口隔离**
```cpp
// 用户只需要知道两个函数
bool loadModel(const std::string& model_path);
std::string generate(const std::string& prompt, int max_tokens);

// 内部实现细节隐藏
private:
    std::vector<llama_token> tokenize(...);
    bool process_prompt(...);
    std::string generate_tokens(...);
```

3. **上下文管理**
```cpp
std::string generate(const std::string& prompt, int max_tokens) {
    // 每次调用创建新上下文（无历史记录）
    llama_context* ctx = llama_new_context_with_model(model_, ctx_params);

    // ... 推理 ...

    llama_free(ctx);  // 函数结束前释放
    return output;
}
```

### 5.3 完整代码流程

```cpp
// main.cpp中的使用
SimpleInference inference;

// 步骤1：加载模型
if (!inference.loadModel("/app/models/smollm-360m-q4.gguf")) {
    return 1;
}

// 步骤2：生成文本
std::string prompt = "What is AI?";
std::string result = inference.generate(prompt, 64);

// 步骤3：返回结果
std::cout << "Result: " << result << "\n";
```

**内部执行流程**：

```
inference.generate("What is AI?", 64)
    ↓
[1] 创建推理上下文
    llama_context* ctx = llama_new_context_with_model(model_, params);

[2] Tokenization
    tokens = tokenize(vocab, "What is AI?");
    // [1841, 374, 15592, 30]

[3] 处理Prompt
    process_prompt(ctx, tokens);
    ├─ 创建batch
    ├─ 填充token、pos、logits
    └─ llama_decode(ctx, batch)

[4] 生成循环
    generate_tokens(ctx, vocab, 64, tokens.size());
    ├─ for (step = 0; step < 64; ++step)
    │   ├─ logits = llama_get_logits(ctx)
    │   ├─ next_token = argmax(logits)
    │   ├─ piece = token_to_piece(next_token)
    │   ├─ output += piece
    │   ├─ llama_decode(ctx, {next_token})
    │   └─ n_past++
    └─ return output

[5] 清理
    llama_free(ctx);

[6] 返回结果
    return " artificial intelligence."
```

---

## 小结

本部分详细讲解了llama.cpp推理引擎：

1. **为什么选择llama.cpp**
   - 纯C++，零依赖
   - CPU优化，量化支持
   - 简单部署，性能优异

2. **llama.cpp架构**
   - 高层API（llama.h）
   - 底层引擎（ggml.h）
   - SIMD优化

3. **核心API详解**
   - 模型加载：llama_load_model_from_file
   - 创建上下文：llama_new_context_with_model
   - Tokenization：llama_tokenize
   - 推理执行：llama_decode
   - 获取输出：llama_get_logits, llama_token_to_piece

4. **版本兼容性**
   - API变更历史
   - 解决方案（锁定版本/修改代码）
   - 版本管理最佳实践

5. **SimpleInference封装**
   - 设计理念（隐藏复杂性）
   - 类设计（RAII、接口隔离）
   - 完整流程

**下一部分预告**：Server-11的Docker集成、模型选择与优化、HTTP API设计等实战内容。

---

**继续阅读**：[第四部分：Server-11实现详解](./第四部分-Server11实现详解.md)
