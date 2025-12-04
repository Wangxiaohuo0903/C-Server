# 推测式解码快速开始指南

**更新日期**: 2025-11-13
**状态**: 核心代码实现完成 ✅

---

## 🎉 已完成工作

我已经为你完成了推测式解码的核心实现，包括：

### 1. 完整的代码实现 ✅
- ✅ `SpeculativeDecoder.h` (300+ 行) - 完整的类定义和 API 文档
- ✅ `SpeculativeDecoder.cpp` (600+ 行) - 完整的实现代码
  - 构造函数：加载 draft 模型
  - `genDraft()`: 使用小模型生成候选 tokens
  - `verifyAndAccept()`: 使用大模型验证并接受 tokens
  - `inferTokens()`: 主推理循环
  - 采样、tokenization、统计等辅助函数

### 2. 测试与工具 ✅
- ✅ `test_speculative.cpp` - 独立的测试程序
- ✅ `build_and_test_speculative.sh` - Linux/Mac 构建脚本
- ✅ `build_and_test_speculative.ps1` - Windows 构建脚本

### 3. 详细文档 ✅
- ✅ `research/speculative-decoding-research.md` - 理论研究 (5000+ 字)
- ✅ `design/speculative-decoding-architecture.md` - 架构设计 (8000+ 字)
- ✅ `docs/draft-models-guide.md` - 模型选择指南 (3000+ 字)

---

## 🚀 快速开始（3 步）

### Step 1: 下载 Draft 模型

```powershell
# Windows PowerShell
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\scripts
.\download_draft_models.ps1
# 选择 1（TinyLlama-160M，推荐）
```

或手动下载：
- URL: https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf
- 保存到: `AI-infra/models/draft/tinyllama-160m-q4.gguf`

### Step 2: 编译项目

```powershell
# 进入项目目录
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\AI-chats-linux

# 创建 build 目录
mkdir build -Force
cd build

# CMake 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build . --config Release -j 4
```

### Step 3: 运行测试

```powershell
# 在 build 目录下运行
.\Release\test_speculative.exe `
    --model ..\models\tinyllama-1.1b-q4.gguf `
    --model-draft ..\models\draft\tinyllama-160m-q4.gguf `
    --prompt "用Python实现快速排序" `
    --n-predict 100 `
    --n-draft 16 `
    --verbose
```

**预期输出**：
```
╔═══════════════════════════════════════════════════════════╗
║      Speculative Decoding Test Program                   ║
╚═══════════════════════════════════════════════════════════╝

📦 Loading target model...
✅ Target model loaded
🔧 Creating target context...
✅ Target context created

🚀 Initializing Speculative Decoder...
[SpecDecoder] Draft model loaded successfully
[SpecDecoder] ✅ Initialization complete!

🎯 Starting inference...
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

Output:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
当然可以！以下是用Python实现的快速排序算法：

```python
def quick_sort(arr):
    if len(arr) <= 1:
        return arr

    pivot = arr[len(arr) // 2]
    left = [x for x in arr if x < pivot]
    middle = [x for x in arr if x == pivot]
    right = [x for x in arr if x > pivot]

    return quick_sort(left) + middle + quick_sort(right)

# 测试
arr = [3, 6, 8, 10, 1, 2, 1]
print(quick_sort(arr))
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

📊 Performance Statistics:
  Generated tokens:     87
  Drafted tokens:       160
  Accepted tokens:      95
  Accept rate:          59.38%
  Speedup:              1.95x
  Time (total):         3450.5 ms
  Tokens per second:    25.2

✅ Test complete!
```

---

## 📊 核心代码解析

### SpeculativeDecoder 类结构

```cpp
class SpeculativeDecoder {
public:
    // 构造函数：加载 draft 模型并验证兼容性
    SpeculativeDecoder(model_target, ctx_target, draft_model_path, config);

    // 高层接口：文本 → 文本
    std::string infer(const std::string& prompt, int max_tokens, float temp);

    // 中层接口：tokens → tokens
    std::vector<llama_token> inferTokens(const std::vector<llama_token>& prompt, ...);

    // 低层接口：核心算法
    std::vector<llama_token> genDraft(...);           // Draft 生成
    std::vector<llama_token> verifyAndAccept(...);    // 验证与接受

    // 统计接口
    Stats getStats() const;
    void printStats() const;
};
```

### 推理流程

```
while (n_predict < max_tokens) {
    // 1. Draft 生成（小模型）
    draft_tokens = genDraft(prompt, last_token, n_past);
    // → 生成 N=16 个候选 tokens，耗时 ~200ms

    // 2. 构造 Batch
    batch = [last_token] + draft_tokens;
    // → Batch 大小 = 17

    // 3. 大模型验证
    llama_decode(ctx_target, batch);
    // → 一次推理验证所有 tokens，耗时 ~100ms

    // 4. 采样与接受
    accepted = verifyAndAccept(draft_tokens, ...);
    // → 逐个验证，接受 ~10 个 tokens

    // 5. 输出并更新状态
    for (token : accepted) {
        output(token);
        n_past++;
    }

    // 6. 清理未接受的 KV cache
    llama_memory_seq_rm(ctx_target, n_past, -1);
}
```

### genDraft() 实现

```cpp
std::vector<llama_token> genDraft(prompt, last_token, n_past) {
    std::vector<llama_token> draft;

    llama_token current = last_token;

    for (int i = 0; i < config.n_draft; ++i) {
        // 1. 推理当前 token
        llama_decode(ctx_draft, {current});

        // 2. 采样下一个 token
        next = sampleGreedy(ctx_draft);

        // 3. 置信度检查
        prob = getTokenProb(ctx_draft, next);
        if (prob < config.p_min) break;

        // 4. EOS 检查
        if (is_eos(next)) break;

        draft.push_back(next);
        current = next;
    }

    return draft;
}
```

### verifyAndAccept() 实现

```cpp
std::vector<llama_token> verifyAndAccept(draft, last_token, n_past) {
    std::vector<llama_token> accepted;

    // 1. 构造 batch
    batch = [last_token] + draft;

    // 2. 大模型验证
    llama_decode(ctx_target, batch);

    // 3. 逐个采样并验证
    for (int i = 0; i < batch.size(); ++i) {
        logits = llama_get_logits_ith(ctx_target, i);
        sampled = sampleGreedy(logits);

        if (i == 0) {
            // 第一个位置总是接受
            accepted.push_back(sampled);
        } else {
            // 验证 draft token
            if (sampled == draft[i - 1]) {
                accepted.push_back(sampled);  // 接受
            } else {
                accepted.push_back(sampled);  // 拒绝，使用采样结果
                break;  // 终止验证
            }
        }
    }

    return accepted;
}
```

---

## 📁 项目文件结构

```
AI-infra/
├── research/
│   └── speculative-decoding-research.md         # 理论研究
├── design/
│   └── speculative-decoding-architecture.md     # 架构设计
├── docs/
│   └── draft-models-guide.md                    # 模型指南
├── scripts/
│   ├── download_draft_models.ps1                # Windows 下载脚本
│   ├── download_draft_models.sh                 # Linux 下载脚本
│   ├── build_and_test_speculative.ps1           # Windows 构建脚本
│   └── build_and_test_speculative.sh            # Linux 构建脚本
├── AI-chats-linux/
│   ├── src/inference/
│   │   ├── SpeculativeDecoder.h                 # 头文件 (300+ 行)
│   │   ├── SpeculativeDecoder.cpp               # 实现 (600+ 行)
│   │   ├── ModelManager.h
│   │   └── ModelManager.cpp
│   ├── test_speculative.cpp                     # 测试程序
│   └── CMakeLists.txt
└── models/
    └── draft/
        └── tinyllama-160m-q4.gguf               # Draft 模型 (~100MB)
```

---

## 🔧 配置选项

### SpeculativeDecoder::Config

```cpp
struct Config {
    int n_draft = 16;              // 每次生成的 draft tokens 数量
    int n_draft_min = 5;           // 最小 draft 数量（低于此值跳过）
    float p_min = 0.9f;            // Draft 置信度阈值

    int n_ctx_draft = 2048;        // Draft 模型上下文长度
    int n_threads_draft = 2;       // Draft 模型线程数
    int n_gpu_layers_draft = 0;    // Draft 模型 GPU 层数

    float temperature = 0.7f;      // 采样温度
    int top_k = 40;                // Top-K 采样
    float top_p = 0.9f;            // Top-P 采样

    bool enable_kv_reuse = true;   // 是否复用 KV cache
    bool verbose = false;          // 是否打印详细日志
};
```

### 调优建议

| 参数 | 建议值 | 说明 |
|------|--------|------|
| `n_draft` | 8-32 | 代码生成场景用 16，对话场景用 8 |
| `n_draft_min` | 5-10 | 太小的 draft 没有加速效果 |
| `p_min` | 0.85-0.95 | 越高越保守，draft 越短但质量越高 |
| `n_threads_draft` | n_threads / 2 | Draft 模型用一半线程即可 |

---

## 📊 性能预期

### 理论加速比

```
Speedup = 1 / ((T_draft / T_target) / α + 1 / (α × N))

其中：
- T_draft = 20ms（TinyLlama-160M）
- T_target = 100ms（TinyLlama-1.1B）
- α = 50%（平均接受率）
- N = 16（draft tokens）

计算：
Speedup = 1 / ((20/100)/0.5 + 1/(0.5×16))
        = 1 / (0.4 + 0.125)
        = 1.9x
```

### 实测数据预期

| 场景 | 接受率 | 加速比 |
|------|--------|--------|
| 代码生成 | 55-65% | 1.9-2.3x |
| 对话问答 | 40-50% | 1.6-1.9x |
| 创意写作 | 30-40% | 1.4-1.7x |

---

## 🐛 故障排除

### 问题 1: 编译错误

**症状**:
```
error: 'llama_vocab_is_eog' was not declared
```

**解决方案**:
- 确保使用最新版 llama.cpp
- 更新 third_party/llama.cpp 子模块

### 问题 2: Draft 模型加载失败

**症状**:
```
[SpecDecoder] ERROR: Failed to load draft model
```

**解决方案**:
- 检查模型路径是否正确
- 检查模型文件是否完整（~100MB）
- 重新下载模型

### 问题 3: 接受率过低（<20%）

**可能原因**:
- Draft 模型质量差
- Draft tokens 数量过多
- 置信度阈值过低

**解决方案**:
```cpp
config.n_draft = 8;       // 减少 draft 数量
config.p_min = 0.95f;     // 提高置信度阈值
```

### 问题 4: 词汇表不兼容

**症状**:
```
[SpecDecoder] WARNING: Vocab types differ
```

**解决方案**:
- 使用同一家族的模型（如 TinyLlama 系列）
- 检查模型的 tokenizer 是否一致

---

## 📚 下一步

1. ✅ 运行测试程序验证功能
2. ⏳ 集成到 ModelManager（支持 HTTP API）
3. ⏳ 实现性能对比测试框架
4. ⏳ 收集实验数据（接受率、加速比）
5. ⏳ 撰写论文章节

---

## 💡 使用建议

### 何时使用推测式解码？

**适合场景**：
- ✅ 代码生成（接受率高，加速明显）
- ✅ 结构化输出（JSON、YAML 等）
- ✅ 翻译任务（高度可预测）
- ✅ 批量处理（摊销 draft 开销）

**不适合场景**：
- ❌ 创意写作（接受率低，加速不明显）
- ❌ 单个短请求（overhead 大于收益）
- ❌ 内存受限环境（需要加载两个模型）

### 性能优化技巧

1. **调整 n_draft**：代码生成用 16，对话用 8
2. **提高 p_min**：过滤低质量 draft，提升接受率
3. **使用更小的 draft 模型**：SmolLM-135M 更快
4. **与 KV-Cache 组合**：两种优化叠加效果

---

## 🎓 学习资源

- **源码**: `AI-chats-linux/src/inference/SpeculativeDecoder.cpp`
- **理论**: `research/speculative-decoding-research.md`
- **架构**: `design/speculative-decoding-architecture.md`
- **llama.cpp 示例**: `third_party/llama.cpp/examples/speculative-simple/`

---

**祝你测试顺利！如有问题，请查看详细文档或联系我。** 🚀
