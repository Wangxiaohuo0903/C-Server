# Server-14 AI对话系统 - 研究生面试准备文档

## 一、项目概述（30秒电梯演讲）

> **面试开场白建议：**
>
> "这是我独立开发的一个**生产级AI推理服务系统**，基于C++实现，核心亮点是通过**KV Cache会话池化**技术，将多轮对话的推理性能提升了**37倍**。系统包含完整的用户认证（JWT）、数据持久化（SQLite）、高并发网络处理（epoll+线程池），以及支持DeepSeek-R1等大模型的推理能力。"

---

## 二、技术架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        客户端 (Browser)                          │
└─────────────────────────────────────────────────────────────────┘
                              │ HTTP/REST
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                     HttpServer (epoll + ThreadPool)              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ epoll (ET)  │→ │ ThreadPool  │→ │ Router + AuthMiddleware │  │
│  │ 非阻塞I/O   │  │ 工作窃取    │  │ 路由分发 + JWT验证      │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
┌─────────────────┐ ┌─────────────────┐ ┌─────────────────────────┐
│   JWTAuth       │ │   Database      │ │   ModelManagerV2        │
│ HMAC-SHA256签名 │ │ SQLite持久化   │ │ ┌─────────────────────┐ │
│ Token生成/验证  │ │ users/chats/   │ │ │ SessionContextPool  │ │
└─────────────────┘ │ messages表     │ │ │  ┌───────────────┐  │ │
                    └─────────────────┘ │ │  │Session1:ctx  │  │ │
                                        │ │  │Session2:ctx  │  │ │
                                        │ │  │Session3:ctx  │  │ │
                                        │ │  │ (LRU淘汰)    │  │ │
                                        │ │  └───────────────┘  │ │
                                        │ └─────────────────────┘ │
                                        │         │               │
                                        │         ▼               │
                                        │ ┌─────────────────────┐ │
                                        │ │   llama.cpp         │ │
                                        │ │   LLM推理引擎       │ │
                                        │ └─────────────────────┘ │
                                        └─────────────────────────┘
```

---

## 三、高频面试问题与回答

### 3.1 项目整体类问题

#### Q1: 请简单介绍一下你的项目？

**回答要点：**
> 这是一个完整的AI对话服务系统，类似于ChatGPT的后端实现。
>
> **技术栈**：C++20、llama.cpp、epoll、SQLite、JWT
>
> **核心特性**：
> 1. **高性能推理**：通过KV Cache池化，多轮对话性能提升37倍
> 2. **完整认证**：JWT Token认证，支持多用户隔离
> 3. **数据持久化**：SQLite存储用户和聊天记录
> 4. **高并发**：epoll + 线程池，支持1000+并发连接
>
> **项目规模**：约5000行C++代码，16个头文件，7个源文件

---

#### Q2: 为什么选择C++而不是Python？

**回答要点：**
> 1. **性能要求**：LLM推理是计算密集型任务，C++比Python快10-100倍
> 2. **内存控制**：大模型占用数GB内存，C++可以精确控制内存分配和释放
> 3. **llama.cpp生态**：业界最成熟的CPU推理库是C++实现的
> 4. **学习目的**：研究生阶段希望深入理解系统底层，C++是最好的选择
>
> **补充**：Python适合快速原型，但生产环境的推理服务通常用C++/Rust

---

#### Q3: 项目的技术难点是什么？

**回答要点：**
> **最大难点是KV Cache的增量推理优化**。
>
> **问题**：Transformer的注意力机制复杂度是O(n²)，10轮对话后，每次都重新计算会导致延迟指数增长。
>
> **解决方案**：
> 1. 为每个会话维护独立的`llama_context`，保存KV Cache
> 2. 实现**最长公共前缀(LCP)匹配**，只计算新增的token
> 3. 使用**LRU淘汰策略**管理有限的会话池
>
> **效果**：1000 token上下文的第10轮对话，从15.8秒降到0.4秒，**提升37倍**。

---

### 3.2 KV Cache优化（核心亮点）

#### Q4: 什么是KV Cache？为什么能加速推理？

**回答要点：**
> **KV Cache是Transformer注意力机制的中间结果缓存。**
>
> **原理**：
> - Transformer的Self-Attention需要计算Query、Key、Value矩阵
> - 对于已经处理过的token，它们的K和V是固定的
> - 缓存这些K、V矩阵，新token只需要计算自己的Q，然后与缓存的K、V做注意力
>
> **数学表达**：
> ```
> Attention(Q, K, V) = softmax(QK^T / √d) × V
>
> 不使用Cache: 每次计算所有token的K和V
> 使用Cache:   只计算新token的K和V，与历史拼接
> ```
>
> **复杂度对比**：
> - 不使用Cache：O(n²) 每轮
> - 使用Cache：O(n) 增量计算

---

#### Q5: 你的KV Cache池化是怎么实现的？

**回答要点：**
> **核心类：`SessionContextPool`**
>
> ```cpp
> // 数据结构
> struct SessionContext {
>     llama_context* ctx;           // llama.cpp上下文（含KV Cache）
>     std::vector<llama_token> tokens;  // 已处理的token序列
>     int cached_token_count;       // 缓存的token数量
>     long long last_used;          // 最后使用时间（用于LRU）
> };
>
> std::unordered_map<string, SessionContext> session_map_;
> ```
>
> **关键算法**：
> 1. **会话获取**：`getOrCreateSession(session_id)` - 命中返回，未命中创建
> 2. **增量推理**：`processIncrementalTokens()` - 只处理新增token
> 3. **LRU淘汰**：池满时淘汰最久未使用的会话
>
> **最长公共前缀匹配**（代码第159-169行）：
> ```cpp
> for (size_t i = 0; i < min(old_tokens.size(), new_tokens.size()); ++i) {
>     if (old_tokens[i] == new_tokens[i]) {
>         common_prefix_len++;
>     } else {
>         break;  // 遇到第一个不同就停止
>     }
> }
> // 只处理 new_tokens[common_prefix_len:] 部分
> ```

---

#### Q6: LRU淘汰策略是怎么实现的？

**回答要点：**
> **实现方式**：每个SessionContext记录`last_used`时间戳
>
> ```cpp
> void evictLRU() {
>     string lru_session_id;
>     long long min_last_used = LLONG_MAX;
>
>     // 遍历找最小时间戳
>     for (const auto& [session_id, ctx] : session_map_) {
>         if (ctx.last_used < min_last_used) {
>             min_last_used = ctx.last_used;
>             lru_session_id = session_id;
>         }
>     }
>
>     // 释放该会话的context
>     llama_free(session_map_[lru_session_id].ctx);
>     session_map_.erase(lru_session_id);
> }
> ```
>
> **优化空间**（面试加分点）：
> - 当前实现是O(n)遍历，可以用**双向链表+哈希表**优化到O(1)
> - 类似于LeetCode 146题的LRU Cache实现

---

### 3.3 网络与并发

#### Q7: 为什么使用epoll而不是select/poll？

**回答要点：**
> | 特性 | select | poll | epoll |
> |------|--------|------|-------|
> | 最大连接数 | 1024 | 无限制 | 无限制 |
> | 时间复杂度 | O(n) | O(n) | O(1) |
> | 内存拷贝 | 每次全量 | 每次全量 | 只拷贝就绪的 |
> | 触发模式 | 水平 | 水平 | 水平+边缘 |
>
> **epoll优势**：
> 1. **事件驱动**：只返回就绪的fd，不需要遍历全部
> 2. **内核态管理**：红黑树存储fd，回调机制通知就绪事件
> 3. **边缘触发(ET)**：减少系统调用次数
>
> **我的实现**（HttpServer.h第176行）：
> ```cpp
> ev.events = EPOLLIN | EPOLLET;  // 边缘触发
> ```

---

#### Q8: 边缘触发(ET)和水平触发(LT)的区别？

**回答要点：**
> **水平触发(LT)**：只要fd可读/可写，epoll_wait就会返回
>
> **边缘触发(ET)**：只有状态变化时才返回（从不可读变为可读）
>
> **ET的注意事项**（我踩过的坑）：
> 1. **必须一次性读完**：否则不会再次通知
> ```cpp
> while (true) {
>     ssize_t n = read(fd, buf, sizeof(buf));
>     if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
>         break;  // 读完了
>     // 处理数据...
> }
> ```
> 2. **必须非阻塞socket**：否则read会阻塞
>
> **为什么选ET**：减少epoll_wait的调用次数，提高性能

---

#### Q9: 线程池是怎么实现的？

**回答要点：**
> **核心组件**：
> 1. **任务队列**：`std::queue<std::function<void()>>`
> 2. **工作线程**：固定数量的线程循环取任务执行
> 3. **同步原语**：`std::mutex` + `std::condition_variable`
>
> **工作流程**：
> ```cpp
> // 生产者（主线程）
> pool.enqueue([fd] { handleConnection(fd); });
>
> // 消费者（工作线程）
> void worker() {
>     while (running) {
>         std::function<void()> task;
>         {
>             unique_lock<mutex> lock(queue_mutex);
>             cv.wait(lock, [this] { return !tasks.empty() || stop; });
>             task = std::move(tasks.front());
>             tasks.pop();
>         }
>         task();  // 执行任务
>     }
> }
> ```
>
> **设计考虑**：
> - 线程数量：设为CPU核心数（4核设4线程）
> - 队列满时策略：可以阻塞或丢弃，我选择阻塞

---

### 3.4 安全与认证

#### Q10: JWT是怎么工作的？

**回答要点：**
> **JWT结构**：`Header.Payload.Signature`
>
> ```
> Header:  {"alg":"HS256","typ":"JWT"}        → Base64编码
> Payload: {"username":"alice","exp":1234567} → Base64编码
> Signature: HMAC-SHA256(header.payload, secret_key)
> ```
>
> **我的实现流程**：
>
> **生成Token**（JWTAuth.h第26行）：
> ```cpp
> static std::string generateToken(const std::string& username) {
>     string header = R"({"alg":"HS256","typ":"JWT"})";
>     long long exp = getCurrentTimestamp() + 86400;  // 24小时后过期
>     string payload = "{\"username\":\"" + username + "\",\"exp\":" + exp + "}";
>
>     string data = base64UrlEncode(header) + "." + base64UrlEncode(payload);
>     string signature = hmacSHA256(data, secret_key);
>
>     return data + "." + base64UrlEncode(signature);
> }
> ```
>
> **验证Token**：
> 1. 分割为三部分
> 2. 重新计算签名，与token中的签名比较
> 3. 检查是否过期

---

#### Q11: 为什么用HMAC-SHA256而不是RSA？

**回答要点：**
> | 算法 | 类型 | 速度 | 适用场景 |
> |------|------|------|----------|
> | HMAC-SHA256 | 对称加密 | 快 | 单服务/微服务内部 |
> | RS256 (RSA) | 非对称加密 | 慢10-100倍 | 分布式/第三方验证 |
>
> **选择HMAC的原因**：
> 1. 这是单体服务，签发和验证都在同一个服务器
> 2. 性能更好，验证一个token只需要几微秒
> 3. 实现简单，只需要一个密钥
>
> **RSA的优势**：
> - 公钥可以公开，任何人都能验证
> - 适合OAuth2.0等分布式场景

---

### 3.5 数据库设计

#### Q12: 数据库表结构是怎么设计的？

**回答要点：**
> **三张表**：
> ```sql
> -- 用户表
> CREATE TABLE users (
>     username TEXT PRIMARY KEY,
>     password TEXT  -- 存储的是哈希值
> );
>
> -- 聊天会话表
> CREATE TABLE chats (
>     chat_id INTEGER PRIMARY KEY AUTOINCREMENT,
>     username TEXT,      -- 外键，关联用户
>     title TEXT,         -- 会话标题（取第一条消息）
>     session_id TEXT,    -- 关联内存中的KV Cache
>     created DATETIME
> );
>
> -- 消息表
> CREATE TABLE messages (
>     id INTEGER PRIMARY KEY AUTOINCREMENT,
>     chat_id INTEGER,    -- 外键，关联会话
>     role TEXT,          -- "user" 或 "assistant"
>     content TEXT,
>     ts DATETIME
> );
>
> -- 索引优化
> CREATE INDEX idx_session_id ON chats(session_id);
> CREATE INDEX idx_chat_id ON messages(chat_id);
> ```
>
> **设计考虑**：
> - `username`作为用户隔离的依据
> - `session_id`关联内存中的KV Cache，实现冷热数据分离
> - 索引加速常见查询

---

### 3.6 C++语言特性

#### Q13: 项目中用了哪些C++现代特性？

**回答要点：**
> 1. **C++20标准**：
>    - 结构化绑定：`for (auto& [session_id, ctx] : session_map_)`
>    - `std::chrono` 时间库
>
> 2. **智能指针**：
>    - `std::unique_ptr` 管理独占资源
>    - RAII原则：构造时获取，析构时释放
>
> 3. **Lambda表达式**：
>    ```cpp
>    router.addRoute("GET", "/", [](const HttpRequest& req) {
>        return HttpResponse(200, "Hello");
>    });
>    ```
>
> 4. **移动语义**：
>    ```cpp
>    task = std::move(tasks.front());  // 避免拷贝
>    ```
>
> 5. **std::mutex + std::lock_guard**：
>    ```cpp
>    std::lock_guard<std::mutex> lock(mutex_);
>    // 自动加锁解锁，异常安全
>    ```

---

#### Q14: 如何保证线程安全？

**回答要点：**
> **三个层面的保护**：
>
> 1. **SessionContextPool**：`std::mutex mutex_`
>    - 保护`session_map_`的读写
>    - 每个操作都用`lock_guard`加锁
>
> 2. **HttpServer的recvBuffers**：`std::mutex recvBuffersMutex`
>    - 保护per-client的接收缓冲区
>    - 多个线程可能同时处理不同连接
>
> 3. **llama_context**：
>    - 每个会话独立的context，不共享
>    - 避免了推理过程中的竞态条件
>
> **为什么不用读写锁**：
> - 会话操作大多是写操作（更新缓存）
> - `std::shared_mutex`的读锁开销在低竞争场景下反而更大

---

### 3.7 LLM推理相关

#### Q15: llama.cpp的推理流程是怎样的？

**回答要点：**
> **四个步骤**：
>
> 1. **Tokenize**：文本→Token序列
>    ```cpp
>    llama_tokenize(vocab, prompt, tokens, ...);
>    // "你好" → [1234, 5678]
>    ```
>
> 2. **Prefill/Prompt Processing**：处理输入
>    ```cpp
>    llama_batch batch = llama_batch_init(n_tokens, ...);
>    llama_decode(ctx, batch);  // 填充KV Cache
>    ```
>
> 3. **Decode/Generation**：自回归生成
>    ```cpp
>    while (step < max_tokens) {
>        float* logits = llama_get_logits(ctx);
>        int next_token = sample(logits);  // 采样下一个token
>        llama_decode(ctx, next_token);    // 更新KV Cache
>    }
>    ```
>
> 4. **Detokenize**：Token→文本
>    ```cpp
>    llama_token_to_piece(vocab, token, piece, ...);
>    ```

---

#### Q16: 什么是贪婪采样？还有什么其他采样方式？

**回答要点：**
> **采样策略对比**：
>
> | 策略 | 原理 | 特点 |
> |------|------|------|
> | Greedy | 选概率最高的token | 确定性，可能重复 |
> | Top-K | 从前K个中随机选 | 有多样性 |
> | Top-P (Nucleus) | 从累积概率达到P的集合中选 | 动态范围 |
> | Temperature | 调节logits分布的平滑度 | T→0趋向贪婪，T→∞趋向均匀 |
>
> **我的实现**（简单贪婪）：
> ```cpp
> int best = 0;
> float bestv = logits[0];
> for (int v = 1; v < vocab_size; ++v) {
>     if (logits[v] > bestv) {
>         bestv = logits[v];
>         best = v;
>     }
> }
> ```
>
> **为什么用贪婪**：
> - 简单可靠，适合问答场景
> - 生产环境可以加入Temperature参数

---

## 四、可能的深入追问

### 4.1 性能优化方向

**Q: 如果要进一步优化性能，你会怎么做？**

> 1. **批处理推理**：多个请求合并成一个batch，提高GPU利用率
> 2. **量化**：INT8/INT4量化减少内存带宽压力
> 3. **KV Cache压缩**：PagedAttention等技术减少内存占用
> 4. **投机采样**：用小模型预测，大模型验证，提高吞吐

### 4.2 系统扩展

**Q: 如果要支持更多用户，怎么扩展？**

> 1. **垂直扩展**：增加内存，支持更多KV Cache会话
> 2. **水平扩展**：多实例部署，用Redis共享会话状态
> 3. **会话分离**：热会话在内存，冷会话序列化到磁盘
> 4. **负载均衡**：Nginx反向代理，按session_id哈希分配

### 4.3 安全性

**Q: 项目的安全措施有哪些？可以改进吗？**

> **已实现**：
> - JWT认证防止未授权访问
> - 用户数据隔离（按username过滤）
>
> **可改进**：
> - 密码应该用bcrypt/argon2哈希，不是明文
> - JWT密钥应该从环境变量读取
> - 添加请求频率限制（Rate Limiting）
> - HTTPS支持

---

## 五、项目亮点总结（面试收尾）

> "总结一下，这个项目让我深入理解了：
> 1. **系统编程**：epoll、线程池、RAII等底层技术
> 2. **LLM原理**：Transformer注意力机制、KV Cache优化
> 3. **工程实践**：JWT认证、数据库设计、API设计
> 4. **性能优化**：从15秒到0.4秒的37倍性能提升
>
> 这个项目从Server-11一步步迭代到Server-14，每个版本解决一个核心问题，这种渐进式开发的经验对我未来的研究工作很有帮助。"

---

## 六、代码片段速查

### 6.1 KV Cache增量推理核心代码
```cpp
// SessionContextPool.cpp:159-183
size_t common_prefix_len = 0;
for (size_t i = 0; i < min(old_tokens.size(), new_tokens.size()); ++i) {
    if (old_tokens[i] == new_tokens[i]) {
        common_prefix_len++;
    } else {
        break;
    }
}
int tokens_to_process = new_tokens.size() - common_prefix_len;
// 只decode新增的tokens
```

### 6.2 JWT签名生成
```cpp
// JWTAuth.h:44
string signature = hmacSHA256(header + "." + payload, secret_key);
```

### 6.3 epoll事件循环
```cpp
// HttpServer.h:107-126
while (true) {
    int nfds = epoll_wait(epollfd, events.data(), max_events, -1);
    for (int i = 0; i < nfds; ++i) {
        if (events[i].data.fd == server_fd) {
            acceptConnection();  // 新连接
        } else {
            pool.enqueue([fd] { handleConnection(fd); });  // 提交线程池
        }
    }
}
```

---

**祝面试顺利！** 🎓
