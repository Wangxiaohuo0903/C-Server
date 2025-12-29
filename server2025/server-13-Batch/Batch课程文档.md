# Server-13-Batch 课程文档：批处理推理技术详解

## 铺垫概念

### 1. 顺序处理 vs 批处理
- **顺序处理（Sequential Processing）**：
  - 定义：一次只处理一个请求，完成后再处理下一个
  - 特点：简单直观，但无法充分利用硬件资源
  - 类比：餐厅的单线窗口，一个顾客点完餐才能服务下一个

- **批处理（Batch Processing）**：
  - 定义：将多个请求组合成一批，一起处理
  - 特点：提高硬件利用率，降低平均延迟
  - 类比：餐厅的多线窗口 + 后厨批量做菜

### 2. GPU/CPU 并行计算原理
现代 GPU 拥有数千个计算核心，适合并行计算：

```
单个矩阵乘法：GPU 利用率 20%
  [1x1024] × [1024x4096] → 1个任务，大量核心闲置

批量矩阵乘法：GPU 利用率 80%+
  [8x1024] × [1024x4096] → 8个任务并行，核心充分利用
```

**关键观察**：
- GPU 擅长 SIMD（单指令多数据）操作
- 批处理将多个独立任务打包，充分利用并行能力
- 吞吐量提升远超单个请求的延迟增加

### 3. 大语言模型推理的瓶颈
在服务器场景中：
- **计算瓶颈**：模型参数多（7B、13B 等），单次推理耗时长
- **并发瓶颈**：顺序处理导致请求排队，响应慢
- **资源浪费**：单请求处理时，GPU/CPU 大量核心空闲

**示例**：
```
顺序处理 10 个请求：
请求1 (100ms) → 请求2 (100ms) → ... → 请求10 (100ms)
总耗时: 1000ms
GPU 利用率: 20%（大量核心闲置）

批处理 10 个请求：
[请求1, 请求2, ..., 请求10] 并行处理
总耗时: 150ms（批处理略有开销，但远小于顺序处理）
GPU 利用率: 80%+
```

---

## 为什么要引入批处理

### 问题场景：高并发 Web 服务

假设一个 AI 聊天服务：
- 100 个用户同时发送请求
- 每个请求需要 100ms 推理时间
- 顺序处理方式：

```
请求队列: [R1, R2, R3, ..., R100]

处理流程:
  t=0ms:    处理 R1
  t=100ms:  R1 完成, 处理 R2
  t=200ms:  R2 完成, 处理 R3
  ...
  t=9900ms: R99 完成, 处理 R100
  t=10000ms: R100 完成

用户体验:
  R1:   响应时间 100ms    ✓ 可接受
  R50:  响应时间 5000ms   ✗ 太慢
  R100: 响应时间 10000ms  ✗ 不可接受
```

### 批处理的解决方案

引入批处理后：
```
批处理配置:
  - batch_size = 8（每批处理 8 个请求）
  - batch_timeout = 10ms（等待凑批的最长时间）

处理流程:
  t=0ms:    凑齐 [R1-R8], 批处理（耗时 150ms）
  t=150ms:  [R1-R8] 同时完成, 凑齐 [R9-R16], 批处理
  t=300ms:  [R9-R16] 同时完成, 凑齐 [R17-R24], 批处理
  ...

用户体验:
  R1-R8:   响应时间 150ms   ✓ 略慢但可接受
  R49-R56: 响应时间 ~1050ms ✓ 大幅改善
  R93-R100: 响应时间 ~1950ms ✓ 远优于顺序处理

总耗时: 100批 × 150ms = 1950ms（vs 顺序处理的 10000ms）
加速比: 5.1倍
```

### 核心优势
1. **吞吐量提升**：5-10倍（取决于 batch_size 和模型大小）
2. **响应时间均衡**：所有用户体验相对一致
3. **资源利用率高**：GPU/CPU 核心充分利用
4. **成本降低**：相同硬件服务更多用户

---

## 批处理的核心挑战

### 挑战1：多序列管理
批处理需要同时管理多个独立的推理序列：
```
请求1: "你好" → 生成 "世界！"
请求2: "什么是AI" → 生成 "人工智能是..."
请求3: "天气如何" → 生成 "今天晴天"

问题：
  - 三个请求的 prompt 长度不同（2, 4, 4 tokens）
  - 生成长度不同（3, 10, 4 tokens）
  - 如何在一个 batch 中处理？
```

**解决方案**：llama.cpp 的 `seq_id` 机制
- 每个请求分配独立的 `seq_id`
- llama_batch 支持在一次 decode 中处理多个序列
- 每个序列有独立的 KV Cache 和位置索引

### 挑战2：动态批大小
请求到达是随机的，如何组批？
```
场景1：流量低谷
  - 5 秒内只收到 2 个请求
  - 等待 batch_size=8 会导致延迟过高

场景2：流量高峰
  - 瞬间收到 50 个请求
  - 需要分批处理，避免内存溢出
```

**解决方案**：动态批处理策略
```cpp
等待策略:
  - 收集请求到队列
  - 等待条件: (队列长度 >= batch_size) || (等待时间 >= batch_timeout)
  - 满足任一条件即开始批处理

示例:
  batch_size = 8
  batch_timeout = 10ms

  情况A: 5ms 内收到 8 个请求 → 立即批处理（优先吞吐量）
  情况B: 10ms 内只收到 3 个请求 → 批处理这 3 个（优先延迟）
```

### 挑战3：序列长度不一致
批处理中的请求可能在不同时间完成：
```
Batch = [R1, R2, R3]

Step 1: 三个序列都生成 token
Step 2: 三个序列都生成 token
Step 3: R1 遇到 EOS，结束；R2, R3 继续
Step 4: R2 遇到 EOS，结束；R3 继续
Step 5: R3 遇到 EOS，结束

问题：如何高效处理动态数量的活跃序列？
```

**解决方案**：动态 batch 重组
```cpp
每一步生成后:
  1. 检查哪些序列已完成（遇到 EOS 或达到 max_tokens）
  2. 只为未完成的序列构建下一轮 batch
  3. 完成的序列立即返回结果，无需等待其他序列
```

---

## BatchInferenceEngine 核心架构

### 整体设计

```
                    ┌─────────────────────────┐
   用户请求          │   BatchInferenceEngine  │
     ↓              │                         │
  submitRequest()   │  ┌──────────────────┐   │
     ↓              │  │  Request Queue   │   │
  返回 Future       │  │  (线程安全队列)  │   │
     ↓              │  └──────────────────┘   │
  等待结果          │          ↓              │
  (异步阻塞)        │    Worker Thread        │
                    │      (后台运行)          │
                    │          ↓              │
                    │    ┌──────────────┐     │
                    │    │ processBatch │     │
                    │    │  (核心逻辑)  │     │
                    │    └──────────────┘     │
                    │          ↓              │
                    │   设置 Promise 结果     │
                    └─────────────────────────┘
                              ↓
                         Future.get()
                         返回生成文本
```

### 核心数据结构

#### 1. InferenceRequest - 推理请求封装

```cpp
struct InferenceRequest {
    std::string session_id;      // 用户标识（未来可用于 KV Cache）
    std::string prompt;           // 用户输入
    int max_tokens;               // 最大生成长度
    float temperature;            // 采样温度
    std::promise<std::string> result;  // 用于异步返回结果

    InferenceRequest(const std::string& sid, const std::string& p, int mt, float t)
        : session_id(sid), prompt(p), max_tokens(mt), temperature(t) {}
};
```

**设计要点**：
- `std::promise` 实现异步结果传递（生产者-消费者模式）
- 封装请求参数，便于队列传递

#### 2. BatchInferenceEngine - 批处理引擎

```cpp
class BatchInferenceEngine {
private:
    llama_model* model_;           // 共享的模型实例

    // 批处理配置
    int batch_size_;               // 批大小（如 8）
    int batch_timeout_ms_;         // 超时时间（如 10ms）

    // 请求队列（生产者-消费者模式）
    std::queue<std::unique_ptr<InferenceRequest>> request_queue_;
    std::mutex queue_mutex_;       // 保护队列并发访问
    std::condition_variable queue_cv_;  // 通知工作线程

    // 工作线程
    std::thread worker_thread_;
    std::atomic<bool> running_;    // 线程运行标志

    // 统计信息
    std::atomic<int> total_requests_;
    std::atomic<int> total_batches_;
    std::atomic<int64_t> total_batch_time_ms_;

public:
    // 提交请求，返回 Future
    std::future<std::string> submitRequest(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature
    );

private:
    // 工作线程主循环
    void workerLoop();

    // 处理一批请求（核心逻辑）
    void processBatch(std::vector<std::unique_ptr<InferenceRequest>>& batch);
};
```

---

## 核心方法详解

### 1. submitRequest - 提交请求

```cpp
std::future<std::string> BatchInferenceEngine::submitRequest(
    const std::string& session_id,
    const std::string& prompt,
    int max_tokens,
    float temperature
) {
    // 1. 创建请求对象
    auto request = std::make_unique<InferenceRequest>(session_id, prompt, max_tokens, temperature);

    // 2. 获取 Future（用于异步获取结果）
    auto future = request->result.get_future();

    // 3. 将请求加入队列（线程安全）
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        request_queue_.push(std::move(request));  // 转移所有权
        total_requests_.fetch_add(1, std::memory_order_relaxed);
    }

    // 4. 通知工作线程有新任务
    queue_cv_.notify_one();

    // 5. 返回 Future（调用者可以异步等待结果）
    return future;
}
```

**流程图**：
```
用户线程:
  submitRequest("你好", ...) → 返回 Future
      ↓
  继续执行其他代码（非阻塞）
      ↓
  result = future.get() ← 阻塞等待结果
      ↓
  返回生成的文本

工作线程:
  检测到队列有新请求
      ↓
  收集请求凑批
      ↓
  processBatch() 批处理
      ↓
  设置 Promise 结果 → 唤醒 future.get()
```

### 2. workerLoop - 工作线程主循环

```cpp
void BatchInferenceEngine::workerLoop() {
    while (running_.load()) {
        std::vector<std::unique_ptr<InferenceRequest>> batch;

        // ===== 阶段1: 收集请求 =====
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            // 等待条件: (队列非空) 或 (线程停止)
            // 最多等待 batch_timeout_ms_ 毫秒
            queue_cv_.wait_for(
                lock,
                std::chrono::milliseconds(batch_timeout_ms_),
                [this] { return !request_queue_.empty() || !running_.load(); }
            );

            // 线程停止 且 队列为空 → 退出循环
            if (!running_.load() && request_queue_.empty()) break;

            // 从队列取出请求，直到达到 batch_size 或队列为空
            while (!request_queue_.empty() && (int)batch.size() < batch_size_) {
                batch.push_back(std::move(request_queue_.front()));
                request_queue_.pop();
            }
        }  // 离开作用域，自动释放 lock

        // ===== 阶段2: 批处理 =====
        if (!batch.empty()) {
            processBatch(batch);
        }
    }
}
```

**关键技术**：
1. **条件变量等待**：
   ```cpp
   queue_cv_.wait_for(lock, timeout, predicate);
   ```
   - 原子操作：释放锁 → 等待 → 被唤醒后重新获取锁
   - 超时机制：最多等待 `batch_timeout_ms_`
   - 谓词检查：防止虚假唤醒

2. **动态批大小**：
   ```cpp
   while (!request_queue_.empty() && (int)batch.size() < batch_size_)
   ```
   - 不强制等待 `batch_size` 个请求
   - 超时后即使不满 batch_size 也开始处理

### 3. processBatch - 批处理核心逻辑（最重要）

这是整个批处理引擎的核心，实现了真正的多序列并行推理：

```cpp
void BatchInferenceEngine::processBatch(std::vector<std::unique_ptr<InferenceRequest>>& batch) {
    if (batch.empty()) return;

    auto start_time = std::chrono::steady_clock::now();

    // ===== 1. 创建共享 context =====
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = n_ctx_;
    cp.n_threads = n_threads_;
    llama_context* ctx = llama_new_context_with_model(model_, cp);
    if (!ctx) {
        // 失败处理：设置所有请求的结果为错误
        for (auto& request : batch) {
            request->result.set_value("[ctx_fail]");
        }
        return;
    }

    const llama_model* model = llama_get_model(ctx);
    const llama_vocab* vocab = llama_model_get_vocab(model);
    const int eos_token = llama_vocab_eos(vocab);
    const int vocab_size = llama_vocab_n_tokens(vocab);

    // ===== 2. 为每个请求分配 seq_id 并 tokenize =====
    struct SeqInfo {
        int seq_id;                          // 序列 ID（0, 1, 2, ...）
        std::vector<llama_token> prompt_tokens;  // prompt 的 token 序列
        std::string generated;               // 已生成的文本
        int max_tokens;                      // 最大生成长度
        float temperature;                   // 采样温度
        int n_generated;                     // 已生成的 token 数量
        bool finished;                       // 是否已完成
        int batch_logits_idx;                // 该序列在 batch 中的 logits 位置（关键！）
    };

    std::vector<SeqInfo> sequences;
    sequences.reserve(batch.size());

    int total_prompt_tokens = 0;
    for (size_t i = 0; i < batch.size(); ++i) {
        SeqInfo seq;
        seq.seq_id = (int)i;  // 每个请求分配独立的 seq_id
        seq.max_tokens = batch[i]->max_tokens;
        seq.temperature = batch[i]->temperature;
        seq.n_generated = 0;
        seq.finished = false;

        // Tokenize prompt
        const std::string& prompt = batch[i]->prompt;
        std::vector<llama_token> tok_buf(prompt.size() + 128);
        int n_tok = llama_tokenize(vocab, prompt.c_str(), prompt.size(),
                                   tok_buf.data(), tok_buf.size(), true, false);
        if (n_tok < 0) {
            batch[i]->result.set_value("[tok_fail]");
            seq.finished = true;
        } else {
            seq.prompt_tokens.assign(tok_buf.begin(), tok_buf.begin() + n_tok);
            total_prompt_tokens += n_tok;
        }

        sequences.push_back(seq);
    }

    // ===== 3. 阶段1: 并行处理所有 prompts =====
    llama_batch llama_batch_obj = llama_batch_init(total_prompt_tokens, 0, (int)batch.size());

    // 构建包含所有序列 prompts 的 batch
    llama_batch_obj.n_tokens = 0;
    for (auto& seq : sequences) {
        if (seq.finished) continue;

        for (size_t i = 0; i < seq.prompt_tokens.size(); ++i) {
            int idx = llama_batch_obj.n_tokens;
            llama_batch_obj.token[idx] = seq.prompt_tokens[i];
            llama_batch_obj.pos[idx] = (int)i;         // 该 token 在序列中的位置
            llama_batch_obj.seq_id[idx][0] = seq.seq_id;  // 标记属于哪个序列
            llama_batch_obj.n_seq_id[idx] = 1;

            // 只在最后一个 token 计算 logits
            bool is_last = (i == seq.prompt_tokens.size() - 1);
            llama_batch_obj.logits[idx] = is_last;
            if (is_last) {
                seq.batch_logits_idx = idx;  // ★ 记录该序列的 logits 在 batch 中的位置
            }
            llama_batch_obj.n_tokens++;
        }
    }

    // 一次性 decode 所有 prompts
    if (llama_batch_obj.n_tokens > 0) {
        if (llama_decode(ctx, llama_batch_obj) != 0) {
            llama_batch_free(llama_batch_obj);
            llama_free(ctx);
            for (auto& request : batch) {
                request->result.set_value("[decode_fail]");
            }
            return;
        }
    }

    // ===== 4. 阶段2: 并行生成 tokens =====
    int max_steps = 0;
    for (const auto& seq : sequences) {
        max_steps = std::max(max_steps, seq.max_tokens);
    }

    for (int step = 0; step < max_steps; ++step) {
        // --- 步骤A: 采样 - 为每个未完成的序列从上一轮的 logits 中采样 ---
        std::vector<int> sampled_tokens;
        sampled_tokens.reserve(sequences.size());

        for (auto& seq : sequences) {
            if (seq.finished || seq.n_generated >= seq.max_tokens) {
                seq.finished = true;
                sampled_tokens.push_back(-1);  // 占位
                continue;
            }

            // ★ 关键：使用记录的 batch_logits_idx 获取该序列的 logits
            const float* logits = llama_get_logits_ith(ctx, seq.batch_logits_idx);

            if (!logits) {
                seq.finished = true;
                sampled_tokens.push_back(-1);
                continue;
            }

            // Greedy sampling（简化版）
            int best_token = 0;
            float best_logit = -1e9f;
            for (int v = 0; v < vocab_size; ++v) {
                if (logits[v] > best_logit) {
                    best_logit = logits[v];
                    best_token = v;
                }
            }

            // 检查是否结束
            if (best_token == eos_token) {
                seq.finished = true;
                sampled_tokens.push_back(-1);
                continue;
            }

            // 将 token 转为文本
            char piece[256] = {0};
            int n = llama_token_to_piece(vocab, best_token, piece, sizeof(piece), 0, false);
            std::string token_str(piece, n > 0 ? n : 0);

            // 检查停止条件（ChatML 标记等）
            if (token_str.find("im_end") != std::string::npos ||
                token_str.find("im_start") != std::string::npos ||
                token_str.find("<|") != std::string::npos ||
                token_str.find("|>") != std::string::npos ||
                token_str.find("###") != std::string::npos) {
                seq.finished = true;
                sampled_tokens.push_back(-1);
                continue;
            }

            // 添加 token 到输出
            seq.generated += token_str;
            seq.n_generated++;
            sampled_tokens.push_back(best_token);
        }

        // --- 步骤B: 构建下一轮的 batch ---
        llama_batch_obj.n_tokens = 0;
        for (size_t i = 0; i < sequences.size(); ++i) {
            auto& seq = sequences[i];
            if (seq.finished || sampled_tokens[i] == -1) {
                continue;  // 跳过已完成的序列
            }

            int idx = llama_batch_obj.n_tokens;
            llama_batch_obj.token[idx] = sampled_tokens[i];
            llama_batch_obj.pos[idx] = (int)seq.prompt_tokens.size() + seq.n_generated - 1;
            llama_batch_obj.seq_id[idx][0] = seq.seq_id;
            llama_batch_obj.n_seq_id[idx] = 1;
            llama_batch_obj.logits[idx] = true;
            seq.batch_logits_idx = idx;  // ★ 更新该序列在新 batch 中的 logits 位置
            llama_batch_obj.n_tokens++;
        }

        // 如果所有序列都已完成，提前退出
        if (llama_batch_obj.n_tokens == 0) {
            break;
        }

        // Decode 下一轮 tokens
        if (llama_decode(ctx, llama_batch_obj) != 0) {
            break;
        }
    }

    // ===== 5. 设置结果 =====
    for (size_t i = 0; i < batch.size(); ++i) {
        batch[i]->result.set_value(sequences[i].generated);  // Promise 设置结果
    }

    // ===== 6. 清理资源 =====
    llama_batch_free(llama_batch_obj);
    llama_free(ctx);

    // 统计
    auto end_time = std::chrono::steady_clock::now();
    auto batch_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    total_batches_.fetch_add(1, std::memory_order_relaxed);
    total_batch_time_ms_.fetch_add(batch_time_ms, std::memory_order_relaxed);

    std::cout << "[BatchInferenceEngine] Processed batch of " << batch.size()
              << " requests in " << batch_time_ms << "ms" << std::endl;
}
```

---

## 关键技术深度解析

### 1. 多序列推理（Multi-Sequence Inference）

**什么是多序列推理？**

在传统推理中，一次 `llama_decode` 只处理一个序列：
```
单序列:
  batch.token = [101, 102, 103]  // 一个 prompt
  batch.seq_id = [0, 0, 0]       // 都属于序列 0
  batch.pos = [0, 1, 2]          // 位置索引
```

在批处理中，一次 `llama_decode` 同时处理多个序列：
```
多序列:
  batch.token = [101, 102, 201, 202, 203, 301]
  batch.seq_id = [0, 0, 1, 1, 1, 2]
               ↑     ↑        ↑     ↑
             序列0  序列0   序列1   序列2

  batch.pos = [0, 1, 0, 1, 2, 0]
             ↑    ↑  ↑    ↑  ↑  ↑
          序列0   序列1    序列2
          位置0,1  位置0,1,2 位置0
```

**llama.cpp 如何实现？**
- KV Cache 按 `seq_id` 分开存储
- 注意力计算时，每个 token 只与相同 `seq_id` 的历史 tokens 计算
- RoPE 位置编码根据 `pos` 字段独立计算

### 2. batch_logits_idx 的关键作用

**问题背景**：
```cpp
// 错误的代码（Server-13 早期版本的 bug）
for (int i = 0; i < sequences.size(); ++i) {
    const float* logits = llama_get_logits_ith(ctx, i);  // ✗ 错误！
}
```

**为什么错误？**
- `llama_get_logits_ith(ctx, idx)` 的 `idx` 不是序列索引，而是 **batch 中设置了 `logits=true` 的位置索引**
- 例如：batch 中有 10 个 tokens，但只有第 3、7、9 个设置了 `logits=true`
  - `llama_get_logits_ith(ctx, 0)` → 第 3 个 token 的 logits
  - `llama_get_logits_ith(ctx, 1)` → 第 7 个 token 的 logits
  - `llama_get_logits_ith(ctx, 2)` → 第 9 个 token 的 logits

**正确的做法**：
```cpp
// 构建 batch 时记录位置
for (auto& seq : sequences) {
    for (size_t i = 0; i < seq.prompt_tokens.size(); ++i) {
        int idx = llama_batch_obj.n_tokens;
        llama_batch_obj.token[idx] = seq.prompt_tokens[i];
        llama_batch_obj.logits[idx] = (i == seq.prompt_tokens.size() - 1);
        if (llama_batch_obj.logits[idx]) {
            seq.batch_logits_idx = idx;  // ★ 记录实际位置
        }
        llama_batch_obj.n_tokens++;
    }
}

// 获取 logits 时使用记录的位置
const float* logits = llama_get_logits_ith(ctx, seq.batch_logits_idx);  // ✓ 正确
```

**实际示例**：
```
Batch 构建:
  序列0: [101, 102, 103] → idx=0,1,2, logits[2]=true, batch_logits_idx=2
  序列1: [201, 202] → idx=3,4, logits[4]=true, batch_logits_idx=4
  序列2: [301] → idx=5, logits[5]=true, batch_logits_idx=5

获取 logits:
  序列0: llama_get_logits_ith(ctx, 2) → token 103 的 logits
  序列1: llama_get_logits_ith(ctx, 4) → token 202 的 logits
  序列2: llama_get_logits_ith(ctx, 5) → token 301 的 logits
```

### 3. 动态序列管理

**问题**：不同序列在不同时间完成，如何高效处理？

**解决方案**：每步重新构建 batch，只包含未完成的序列
```cpp
// Step 1: 所有序列都在 batch 中
batch.n_tokens = 8 (序列0:3个, 序列1:2个, 序列2:3个)

// Step 2: 序列1 完成，只处理序列0 和序列2
batch.n_tokens = 6 (序列0:3个, 序列2:3个)

// Step 3: 序列0 完成，只处理序列2
batch.n_tokens = 3 (序列2:3个)

// Step 4: 序列2 完成
batch.n_tokens = 0 → 提前退出循环
```

**优势**：
- 避免浪费计算资源在已完成的序列上
- 完成的请求立即返回，无需等待整个 batch

---

## 本节课用到的 C++ 知识

### 1. std::promise 和 std::future - 异步编程

**生产者-消费者模式的现代实现**：

```cpp
// 生产者（工作线程）
void producer() {
    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();

    // 将 future 返回给消费者
    consumer(std::move(future));

    // 异步执行任务
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 设置结果（唤醒消费者）
    promise.set_value("任务完成！");
}

// 消费者（用户线程）
void consumer(std::future<std::string> future) {
    std::cout << "等待结果..." << std::endl;
    std::string result = future.get();  // 阻塞等待
    std::cout << "收到结果: " << result << std::endl;
}
```

**在批处理中的应用**：
```cpp
// 用户线程
std::future<std::string> future = engine.submitRequest("你好", ...);
// ... 可以做其他事情 ...
std::string result = future.get();  // 需要结果时才阻塞

// 工作线程
void processBatch(std::vector<std::unique_ptr<InferenceRequest>>& batch) {
    // ... 批处理 ...
    batch[0]->result.set_value(generated_text);  // 唤醒 future.get()
}
```

### 2. std::condition_variable - 条件等待

**为什么需要条件变量？**

错误的做法（忙等待）：
```cpp
while (running_) {
    if (request_queue_.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // 浪费 CPU
        continue;
    }
    // 处理请求...
}
```

正确的做法（条件变量）：
```cpp
std::unique_lock<std::mutex> lock(queue_mutex_);
queue_cv_.wait_for(
    lock,
    std::chrono::milliseconds(batch_timeout_ms_),
    [this] { return !request_queue_.empty() || !running_.load(); }
);
```

**条件变量的工作原理**：
```
1. wait_for() 被调用时:
   a. 原子性地释放 lock
   b. 线程进入休眠（不消耗 CPU）

2. 被唤醒的三种情况:
   a. 其他线程调用 notify_one() 或 notify_all()
   b. 超时（batch_timeout_ms_）
   c. 虚假唤醒（spurious wakeup）

3. 被唤醒后:
   a. 重新获取 lock
   b. 检查谓词（predicate）
   c. 如果谓词为 true，继续执行
   d. 如果谓词为 false，继续等待（防止虚假唤醒）
```

### 3. std::atomic - 无锁原子操作

**为什么使用 atomic？**

统计信息需要多线程访问：
```cpp
// 错误：数据竞争
int total_requests = 0;  // ✗
void increment() {
    total_requests++;  // 非原子操作，多线程不安全
}

// 正确：原子操作
std::atomic<int> total_requests{0};  // ✓
void increment() {
    total_requests.fetch_add(1, std::memory_order_relaxed);  // 原子操作
}
```

**内存序（Memory Order）**：
- `memory_order_relaxed`：最宽松，只保证原子性，不保证顺序
- `memory_order_acquire/release`：保证同步顺序
- `memory_order_seq_cst`：最严格，顺序一致性（默认）

对于统计计数，`relaxed` 足够：
```cpp
total_requests_.fetch_add(1, std::memory_order_relaxed);
```

### 4. std::unique_lock vs std::lock_guard

**std::lock_guard**（简单场景）：
```cpp
void simple() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 自动加锁
    // ... 临界区代码 ...
    // 离开作用域，自动解锁
}
```

**std::unique_lock**（复杂场景）：
```cpp
void complex() {
    std::unique_lock<std::mutex> lock(mutex_);
    // 自动加锁

    condition.wait(lock);  // 条件变量需要 unique_lock

    // ... 临界区代码 ...

    lock.unlock();  // 可以手动解锁

    // ... 非临界区代码 ...

    lock.lock();  // 可以重新加锁
}
```

**区别**：
- `lock_guard`：轻量级，不可手动解锁
- `unique_lock`：功能强大，支持手动解锁、转移所有权、与条件变量配合

### 5. std::move 和 std::unique_ptr

**避免拷贝大对象**：
```cpp
// InferenceRequest 包含 std::promise，不可拷贝
std::unique_ptr<InferenceRequest> request = std::make_unique<InferenceRequest>(...);

// 转移所有权（移动）
request_queue_.push(std::move(request));  // request 变为 nullptr

// 从队列取出（移动）
auto req = std::move(request_queue_.front());
request_queue_.pop();
```

**unique_ptr 的优势**：
- 独占所有权，自动释放内存
- 不可拷贝，只能移动（防止意外拷贝）
- 零开销（编译时优化）

---

## 性能优化技巧

### 1. 合理设置 batch_size

**权衡因素**：
- **batch_size 太小**：
  - 并行度不足，GPU 利用率低
  - 吞吐量提升有限

- **batch_size 太大**：
  - 内存占用高（KV Cache 线性增长）
  - 凑批等待时间长，延迟增加
  - 超过硬件限制导致 OOM

**推荐值**：
```cpp
// GPU 推理
batch_size = 8-16  // NVIDIA T4, 8GB 显存
batch_size = 32-64 // NVIDIA A100, 40GB 显存

// CPU 推理
batch_size = 4-8   // 根据 CPU 核心数调整
```

### 2. 动态调整 batch_timeout

**策略**：
```cpp
// 流量低谷：快速响应优先
if (queue_length < batch_size / 2) {
    batch_timeout = 5ms;  // 缩短等待时间
}

// 流量高峰：吞吐量优先
if (queue_length >= batch_size) {
    batch_timeout = 0ms;  // 立即处理
}
```

### 3. 提前停止生成

**问题**：某些序列很早就生成 EOS，但需要等待最长的序列
**解决**：动态重组 batch，完成的序列立即返回

```cpp
// 每步检查完成状态
for (auto& seq : sequences) {
    if (seq.finished) {
        // 从下一轮 batch 中移除
        continue;
    }
}
```

---

## 应用场景与最佳实践

### 适用场景
1. **高并发 Web 服务**：多用户同时请求
2. **离线批量处理**：大规模数据标注、文本生成
3. **API 服务**：云端 AI 推理服务
4. **负载均衡**：多个服务器共享请求队列

### 不适用场景
1. **单用户应用**：无并发，批处理无优势
2. **实时交互**：对延迟极敏感（如游戏 NPC）
3. **极长文本生成**：单个请求已占满 GPU

### 最佳实践
1. **监控批处理效率**：
   ```cpp
   double avg_batch_size = total_requests / total_batches;
   if (avg_batch_size < batch_size * 0.5) {
       // 考虑减小 batch_timeout 或 batch_size
   }
   ```

2. **负载均衡**：
   - 多个 BatchInferenceEngine 实例
   - 请求路由到负载较低的实例

3. **优雅降级**：
   ```cpp
   if (request_queue_.size() > max_queue_length) {
       return future_with_error("服务繁忙，请稍后重试");
   }
   ```

---

## 总结

### 核心要点
1. **批处理本质**：将多个独立任务打包，充分利用 GPU/CPU 并行能力
2. **多序列推理**：llama_batch 支持同时处理多个序列，每个序列有独立的 seq_id 和 KV Cache
3. **异步架构**：Promise/Future 实现生产者-消费者模式，用户线程不阻塞
4. **性能提升**：5-10倍吞吐量提升，适合高并发场景

### 技术栈
- llama.cpp API：`llama_batch`, `seq_id`, `llama_get_logits_ith`
- C++ 异步编程：`std::promise`, `std::future`, `std::condition_variable`
- 数据结构：`std::queue`, `std::vector`, `std::unique_ptr`
- 并发控制：`std::mutex`, `std::atomic`

### 与 Server-13-KVcache 的关系
- **KVcache**：优化单个序列的多轮对话（时间维度优化）
- **Batch**：优化多个序列的并行处理（空间维度优化）
- **结合使用**：BatchInferenceEngine + SessionContextPool → 最强性能

---

**课程设计：** 参考 Server-12 多线程课程文档风格
**适用对象：** 理解 C++ 基础、多线程编程、异步编程的开发者
