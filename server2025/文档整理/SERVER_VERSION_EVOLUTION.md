# Server 版本演进详细文档

本文档详细记录了从 Server-11 到 Server-14 每个版本的**完整代码结构**、**新增功能**及其**对应代码实现**，以及版本间的演进关系。

---

## 目录

1. [版本概览](#版本概览)
2. [Server-11-LLM：基础AI推理](#server-11-llm基础ai推理)
3. [Server-12-MultiChat：多轮对话](#server-12-multichat多轮对话)
4. [Server-13-KVcache：性能优化](#server-13-kvcache性能优化)
5. [Server-14：生产级系统](#server-14生产级系统)
6. [版本间核心差异对比](#版本间核心差异对比)
7. [历史遗留代码分析](#历史遗留代码分析)
8. [清理建议](#清理建议)

---

## 版本概览

| 版本 | 代码行数 | 文件数 | 核心特性 |
|------|---------|--------|----------|
| Server-11-LLM | 1,527 | 9 | 单轮AI推理 |
| Server-12-MultiChat | 3,321 | 14 | 多轮对话 + 会话管理 |
| Server-13-KVcache | 3,817 | 14 | KV Cache 优化 |
| Server-14 | 5,883 | 27 | JWT认证 + 持久化 + 对象池 + 流式输出 |

### 增量代码统计

```
Server-11 → Server-12: +1,794 行 (+5 文件)
Server-12 → Server-13: +496 行 (+0 文件)
Server-13 → Server-14: +2,066 行 (+13 文件)
```

### 各版本功能对照表

| 功能 | S-11 | S-12 | S-13 | S-14 |
|------|------|------|------|------|
| 单轮推理 | ✅ | ✅ | ✅ | ✅ |
| 多轮对话 | ❌ | ✅ | ✅ | ✅ |
| 会话管理 | ❌ | ✅ | ✅ | ✅ |
| ChatGPT风格UI | ❌ | ✅ | ✅ | ✅ |
| KV Cache优化 | ❌ | ❌ | ✅ | ✅ |
| JWT认证 | ❌ | ❌ | ❌ | ✅ |
| SQLite持久化 | ❌ | ❌ | ❌ | ✅ |
| 用户隔离 | ❌ | ❌ | ❌ | ✅ |
| Context池(LRU) | ❌ | ❌ | ❌ | ✅ |
| 流式输出(SSE) | ❌ | ❌ | ❌ | ✅ |
| 批处理推理 | ❌ | ❌ | ❌ | ✅(可选) |
| DeepSeek-R1思考过程分离 | ❌ | ❌ | ❌ | ✅ |

---

## Server-11-LLM：基础AI推理

### 文件结构

```
server-11-LLM/
├── main.cpp              # 117行 - 程序入口，初始化模型和服务器
├── SimpleInference.h     # 459行 - ★ 核心：LLM推理引擎封装
├── HttpServer.h          # ~200行 - HTTP服务器（epoll事件循环 + 线程池）
├── HttpRequest.h         # ~200行 - HTTP请求解析（状态机解析器）
├── HttpResponse.h        # ~100行 - HTTP响应构建（状态码 + MIME类型）
├── Router.h              # ~300行 - 路由分发（方法 + 路径匹配）
├── Database.h            # ~200行 - SQLite封装（用户注册/登录）
├── ThreadPool.h          # ~100行 - 线程池（工作窃取调度）
└── Logger.h              # ~50行 - 日志工具（文件 + 控制台）
```

### 核心模块详解

#### 1. SimpleInference.h — LLM推理引擎

这是 Server-11 的核心文件，封装了 llama.cpp 的完整推理流程。

**类定义：**
```cpp
// SimpleInference.h:22-36
class SimpleInference {
public:
    SimpleInference() : model_(nullptr) {}
    ~SimpleInference() {
        if (model_) llama_model_free(model_);
    }

private:
    llama_model* model_;          // GGUF模型句柄（只读资产，可被多个context共用）
    bool has_chat_template_;      // 模型是否内置聊天模板
};
```

**关键方法：**

**(1) loadModel() — 模型加载** (`SimpleInference.h:44-93`)
```cpp
bool loadModel(const std::string& model_path) {
    // 1. 设置模型参数（默认即可）
    llama_model_params model_params = llama_model_default_params();
    // 2. 从GGUF文件加载模型权重 + 词表 + 元数据
    model_ = llama_model_from_file(model_path.c_str(), model_params);
    // 3. 检测是否有内置chat template
    has_chat_template_ = (llama_model_chat_template(model_, nullptr, nullptr, 0) > 0);
    return model_ != nullptr;
}
```

**(2) generate() — 单轮推理入口** (`SimpleInference.h:110-191`)

这是推理的主流程，包含5个关键步骤：

```cpp
std::string generate(const std::string& prompt, int max_tokens = 64,
                     bool use_chat_template = true,
                     const std::string& system_prompt = "") {
    // Step 1: 构建 full_prompt（可选应用chat template）
    std::string full_prompt = prompt;
    if (use_chat_template && has_chat_template_) {
        full_prompt = applyChatTemplate(prompt, system_prompt);
    }

    // Step 2: 创建推理上下文（包含KV Cache）
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;       // 上下文长度
    ctx_params.n_threads = 4;      // CPU线程数
    ctx_params.n_batch = 512;      // prefill批大小
    llama_context* ctx = llama_init_from_model(model_, ctx_params);

    // Step 3: tokenize（文本 → token序列）
    std::vector<llama_token> tokens = tokenize(vocab, full_prompt);

    // Step 4: prefill（把所有prompt token喂入模型，填充KV Cache）
    process_prompt(ctx, tokens);

    // Step 5: decode loop（自回归生成）
    std::string output = generate_tokens(ctx, vocab, max_tokens, tokens.size());

    llama_free(ctx);  // ★ 每次调用都新建并释放context，不保留状态
    return output;
}
```

> **关键限制**：每次 `generate()` 调用都新建 `llama_context` 并在结束时释放。这意味着**不保留多轮对话的KV Cache**，每次推理都要从头处理所有token。

**(3) applyChatTemplate() — 聊天模板渲染** (`SimpleInference.h:205-280`)
```cpp
std::string applyChatTemplate(const std::string& user_msg,
                               const std::string& system_prompt) {
    // 构建消息数组：[system, user]
    std::vector<llama_chat_message> messages;
    if (!system_prompt.empty())
        messages.push_back({"system", system_prompt.c_str()});
    messages.push_back({"user", user_msg.c_str()});

    // 调用 llama.cpp API 渲染模板（如 ChatML、DeepSeek 等格式）
    int len = llama_chat_apply_template(nullptr, tmpl, messages.data(),
                                         messages.size(), true, buf, buf_size);
    return std::string(buf, len);
}
```

**(4) process_prompt() — Prefill阶段** (`SimpleInference.h:330-352`)
```cpp
bool process_prompt(llama_context* ctx, const std::vector<llama_token>& tokens) {
    // 创建 batch 结构体，一次性喂入所有 prompt token
    llama_batch batch = llama_batch_init(tokens.size(), 0, 1);
    for (int i = 0; i < (int)tokens.size(); i++) {
        batch.token[i] = tokens[i];
        batch.pos[i] = i;                    // 位置编码
        batch.logits[i] = (i == (int)tokens.size() - 1);  // 只需最后一个token的logits
    }
    batch.n_tokens = tokens.size();

    int ret = llama_decode(ctx, batch);  // ★ 核心：执行prefill，填充KV Cache
    llama_batch_free(batch);
    return ret == 0;
}
```

**(5) generate_tokens() — 自回归解码循环** (`SimpleInference.h:368-457`)
```cpp
std::string generate_tokens(llama_context* ctx, const llama_vocab* vocab,
                             int max_tokens, int n_past_init) {
    std::string result;
    int n_past = n_past_init;  // 从prompt结束位置开始

    for (int i = 0; i < max_tokens; i++) {
        // 1. 获取最后一个位置的 logits（概率分布）
        float* logits = llama_get_logits(ctx);

        // 2. 贪心采样：选择概率最高的 token
        llama_token new_token_id = 0;
        float max_logit = logits[0];
        for (int j = 1; j < n_vocab; j++) {
            if (logits[j] > max_logit) {
                max_logit = logits[j];
                new_token_id = j;
            }
        }

        // 3. 停止条件：EOS token 或特殊标记
        if (llama_vocab_is_eog(vocab, new_token_id)) break;

        // 4. token → 文本，追加到结果
        char buf[64];
        int len = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
        result.append(buf, len);

        // 5. 将新token喂回模型（decode 1 token），更新KV Cache
        llama_batch next = llama_batch_init(1, 0, 1);
        next.token[0] = new_token_id;
        next.pos[0] = n_past++;
        next.logits[0] = true;
        next.n_tokens = 1;
        llama_decode(ctx, next);
        llama_batch_free(next);
    }
    return result;
}
```

#### 2. HttpServer.h — HTTP服务器

基于 Linux epoll 的事件驱动 HTTP 服务器。

```cpp
// HttpServer.h 核心结构
class HttpServer {
public:
    HttpServer(int port, int max_events, Database& db);

    void start(SessionManager& sessionManager);  // 主事件循环

private:
    void setupServerSocket();     // 创建TCP socket, bind, listen
    void setupEpoll();            // 创建epoll实例，注册server socket
    void acceptConnection();      // accept新连接，加入epoll
    void handleConnection(int fd); // 读取请求 → 路由 → 发送响应

    int server_fd_, epoll_fd_;    // socket和epoll文件描述符
    Router router_;               // 路由器
    ThreadPool* pool_;            // 线程池(4线程)
};
```

**事件循环** (`HttpServer.h:107-125`)：
```
while(running) {
    n = epoll_wait(epoll_fd_, events, max_events, -1);  // 阻塞等待事件
    for each event:
        if (server_fd)  → acceptConnection()   // 新连接
        else            → pool->enqueue(handleConnection, fd)  // 交给线程池
}
```

#### 3. Router.h — 路由分发

```cpp
class Router {
    std::unordered_map<std::string, HandlerFunc> routes;  // "METHOD|PATH" → 处理函数

    void addRoute(method, path, handler);
    HttpResponse routeRequest(const HttpRequest& req);
};
```

**Server-11 API端点：**

| 接口 | 方法 | 功能 |
|------|------|------|
| `/register` | POST | 用户注册 |
| `/login` | POST | 用户登录 |
| `/infer-simple` | POST | 单轮AI推理 |
| `/*.html` | GET | 静态页面 |

---

## Server-12-MultiChat：多轮对话

### 核心改进

相比 Server-11 的单轮推理，Server-12 实现了**完整的多轮对话系统**：
- 新增 `SessionManager` 管理多个会话
- 新增 `ModelManager` + `ChatSession` 维护对话上下文
- 新增 ChatGPT 风格的前端 UI
- 每个会话独立的对话历史

### 文件结构（新增部分用 ★ 标记）

```
server-12-MultiChat/
├── main.cpp                     # 程序入口
├── include/
│   ├── SessionManager.h         # ★ 429行 - 会话管理器（全新）
│   ├── FileUtils.h              # ★ ~50行 - 文件工具（全新）
│   ├── HttpServer.h             # HTTP服务器（继承自S-11）
│   ├── HttpRequest.h            # 请求解析（继承自S-11）
│   ├── HttpResponse.h           # 响应构建（继承自S-11）
│   ├── Router.h                 # 路由分发（扩展新API）
│   ├── Database.h               # SQLite封装（继承自S-11）
│   ├── ThreadPool.h             # 线程池（继承自S-11）
│   └── Logger.h                 # 日志工具
├── src/
│   └── inference/
│       ├── ModelManager.h       # ★ 314行 - 模型管理器（全新）
│       └── ModelManager.cpp     # ★ ~400行 - 推理实现（全新）
└── UI/
    ├── multichat.html           # ★ 328行 - ChatGPT风格UI（全新）
    └── multichat.css            # ★ 378行 - 暗色主题样式（全新）
```

### 核心模块详解

#### 1. SessionManager.h — 会话管理器

管理所有会话的生命周期：创建、消息存储、历史查询、删除。

**数据结构：**
```cpp
// SessionManager.h:35-46 - 消息结构
struct Message {
    std::string role;       // "user" | "assistant"
    std::string content;    // 消息文本
    long long timestamp;    // 毫秒级时间戳（自动设置）
};

// SessionManager.h:51-143 - 会话结构
struct Session {
    std::string session_id;           // 唯一标识（如 "sess_a1b2c3d4"）
    std::vector<Message> history;     // 完整对话历史
    std::string title;                // 标题（自动取自第一条消息前30字符）
    long long created_at, last_active;

    void addMessage(role, content);           // 追加消息，自动更新标题
    std::string getFullContext();              // 所有历史 → ChatML格式
    std::string getRecentContext(int n_turns); // 最近N轮 → ChatML格式
};
```

**SessionManager 类** (`SessionManager.h:166-428`)：
```cpp
class SessionManager {
public:
    std::string createSession();                     // 生成 sess_XXXXXXXX ID
    void addUserMessage(session_id, content);        // 添加用户消息
    void addAssistantMessage(session_id, content);   // 添加AI回复
    std::string getAllSessions();                     // 所有会话列表（JSON）
    std::string getSessionHistory(session_id);       // 会话历史（JSON数组）
    void deleteSession(session_id);                  // 删除会话
    void updateSessionTitle(session_id, new_title);  // 修改标题

private:
    std::unordered_map<std::string, Session> sessions_;  // session_id → Session
    std::mutex mutex_;                                    // 线程安全
};
```

> **限制**：所有会话数据只存在内存中，服务器重启后丢失。这个问题在 Server-14 通过 SQLite 持久化解决。

#### 2. ModelManager.h — 模型管理器 + ChatSession

**ChatSession 类** — 对话上下文管理 (`ModelManager.h:44-149`)

```cpp
class ChatSession {
public:
    // 追加消息并自动裁剪历史
    void add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        prune();  // 限制上下文长度
    }

    // 将历史序列化为 ChatML 格式的 Prompt
    std::string makePrompt() const {
        std::ostringstream oss;
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n" << m.content << "\n";
        }
        oss << "<|assistant|>\n";  // 提示模型开始生成
        return oss.str();
    }

private:
    std::vector<InferenceMessage> history;

    // 历史裁剪策略
    void prune() {
        const int max_round_keep = 4;       // 最多保留4轮对话
        const int max_tokens_per_msg = 200; // 单条最多200字符

        // 步骤1：截断超长消息
        for (auto& m : history)
            if (m.content.size() > max_tokens_per_msg)
                m.content.resize(max_tokens_per_msg);

        // 步骤2：超过8条时删除最早一轮，插入摘要
        while (history.size() > max_round_keep * 2) {
            history.erase(history.begin(), history.begin() + 2);
            history.insert(history.begin(), {"system", "[summary] previous conversation ..."});
        }
    }
};
```

**ModelManager 类** — 推理引擎（单例） (`ModelManager.h:172-313`)

```cpp
class ModelManager {
public:
    static ModelManager& instance();  // 单例模式

    bool loadModel(const std::string& path, int n_ctx = 2048, int n_threads = 4);

    // ★ 多轮对话推理（Server-12 核心接口）
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens, float temperature);

    // 无状态推理（不保留会话）
    std::string raw_infer(const std::string& prompt, int maxTokens, float temperature);

    void dropSession(const std::string& chat_id);  // 释放会话

private:
    llama_model* model_;
    std::unordered_map<std::string, ChatSession> chat_sessions_;  // chat_id → ChatSession
    std::mutex mutex_;
};
```

**ModelManager.cpp 中的 infer() 五步流程** (`ModelManager.cpp:~130-250`)：

```
infer(chat_id, user_msg, maxTokens, temperature):
  Step 1: 获取或创建 ChatSession
  Step 2: session.add("user", user_msg) → 追加消息 + 自动裁剪
  Step 3: prompt = session.makePrompt() → 生成 ChatML 格式
  Step 4: tokens = tokenize(prompt) → 文本转 token
  Step 5: 创建临时 context → prefill → decode → 释放 context
         ★ 每次推理都新建 context，不复用 KV Cache！
```

> **性能瓶颈**：`infer()` 每次调用都创建新的 `llama_context`，需要重新处理所有历史 token。对话越长，推理越慢。

#### 3. Router.h — 新增6个会话管理API

| 接口 | 方法 | 功能 | 实现要点 |
|------|------|------|----------|
| `/api/sessions/new` | POST | 创建新会话 | 调用 `sm.createSession()` |
| `/api/sessions` | GET | 获取所有会话列表 | 返回JSON数组，按最后活跃排序 |
| `/api/sessions/{id}/history` | GET | 获取会话历史 | 返回消息数组 `[{role, content}]` |
| `/api/sessions/{id}/chat` | POST | 发送消息获取回复 | 核心：调用 `mm.infer()` |
| `/api/sessions/{id}/title` | PUT | 修改会话标题 | 更新 Session.title |
| `/api/sessions/{id}` | DELETE | 删除会话 | 同时清理 ModelManager 中的 ChatSession |

#### 4. 前端 UI (multichat.html + multichat.css)

ChatGPT 风格的暗色主题界面：
- **左侧边栏**：会话列表（新增/切换/删除）
- **主聊天区域**：消息显示 + 输入框
- **打字动画**：AI回复时显示三点动画
- **键盘快捷键**：Enter 发送，Shift+Enter 换行
- **自适应高度**：输入框和消息框自动增高

---

## Server-13-KVcache：性能优化

### 核心改进

Server-13 解决了 Server-12 的核心性能瓶颈：**每次推理都重新处理所有历史 token**。

通过持久化 `llama_context`（包含 KV Cache），实现增量推理——只处理新增的 token，复用已缓存的注意力计算结果。

### 文件变化（相比 Server-12，仅修改2个文件）

```
server-13-KVcache/
└── src/inference/
    ├── ModelManager.h    # ★ 修改：新增 KVCachedSession 结构体，ChatSession.add() 返回值变化
    └── ModelManager.cpp  # ★ 修改：增量推理算法，context 持久化
```

### 核心改动详解

#### 1. KVCachedSession 结构体（全新） — `ModelManager.h:198-254`

Server-13 的核心数据结构，为每个会话保存 KV Cache 状态：

```cpp
struct KVCachedSession {
    // ========== 对话历史（同 Server-12）==========
    ChatSession session;

    // ========== KV Cache 状态（Server-13 新增）==========
    llama_context* ctx = nullptr;     // ★ 持久化context（不再每次销毁）
    int n_past = 0;                   // 已处理token数（KV Cache写入位置）
    std::vector<int> cached_tokens;   // 已缓存的token序列（用于验证一致性）

    // 资源管理
    ~KVCachedSession();               // 释放context（~100-500MB）
    KVCachedSession(KVCachedSession&& other) noexcept;  // 移动语义
    KVCachedSession& operator=(KVCachedSession&& other) noexcept;
    KVCachedSession(const KVCachedSession&) = delete;    // 禁止拷贝

    void clearCache();  // 清空KV缓存（当历史被修改时）
};
```

**内存管理**：
- 每个 `llama_context` 占用 ~100-500MB（取决于 n_ctx 大小）
- 使用 RAII 模式：析构时自动释放
- 禁止拷贝（context 不能拷贝），允许移动

#### 2. ChatSession.add() 返回值变化 — `ModelManager.h:61-64`

```cpp
// Server-12: void（无返回值）
void add(const std::string& role, const std::string& content);

// Server-13: bool（指示是否需要清空KV Cache）
bool add(const std::string& role, const std::string& content) {
    history.push_back({role, content});
    return prune();  // ★ 如果prune修改了历史，返回true → 需要清空KV Cache
}
```

```cpp
// prune() 也相应返回 bool
bool prune() {
    bool modified = false;
    // ... 截断超长消息时 modified = true
    // ... 删除早期消息时 modified = true
    return modified;  // ★ 返回是否修改了历史
}
```

> **设计原理**：KV Cache 的内容必须与 token 序列严格一致。如果 `prune()` 删除或截断了历史消息，prompt 会变化，已缓存的 token 就不再有效，必须清空 KV Cache。

#### 3. 会话字典类型变化

```cpp
// Server-12: 只有 ChatSession
std::unordered_map<std::string, ChatSession> chat_sessions_;

// Server-13: 升级为 KVCachedSession（多了 ctx, n_past, cached_tokens）
std::unordered_map<std::string, KVCachedSession> chat_sessions_;
```

#### 4. 增量推理算法 — `ModelManager.cpp` infer() 核心改进

**Server-12 的 infer() 流程：**
```
每次调用:
  1. session.add("user", msg)
  2. prompt = session.makePrompt()     → 全部历史
  3. tokens = tokenize(prompt)         → 全部token
  4. ctx = 新建context
  5. process_prompt(ctx, tokens)       → ★ 处理全部token（重复计算！）
  6. result = generate_tokens(ctx)
  7. llama_free(ctx)                   → ★ 销毁context
```

**Server-13 的 infer() 流程：**
```
每次调用:
  1. session.add("user", msg)
     如果 prune() 返回 true → clearCache()
  2. prompt = session.makePrompt()
  3. new_tokens = tokenize(prompt)
  4. if ctx 不存在 → 创建并保存
  5. 对比 cached_tokens 和 new_tokens:
     - 找到公共前缀长度 common_prefix_len
     - 只处理 new_tokens[common_prefix_len:] 的增量部分  ★ 核心优化！
  6. 更新 n_past 和 cached_tokens
  7. result = generate_tokens(ctx, n_past)  ★ 从缓存位置继续
  8. 不释放 ctx → 保留给下次使用            ★ 持久化
```

**增量token处理的核心代码** (`ModelManager.cpp:~180-240`)：
```cpp
// 找公共前缀
size_t common = 0;
for (size_t i = 0; i < std::min(cached_tokens.size(), new_tokens.size()); i++) {
    if (cached_tokens[i] == new_tokens[i]) common++;
    else break;
}

// 只处理增量部分
int incremental_count = new_tokens.size() - common;
llama_batch batch = llama_batch_init(incremental_count, 0, 1);
for (int i = 0; i < incremental_count; i++) {
    batch.token[i] = new_tokens[common + i];
    batch.pos[i] = common + i;  // 位置从公共前缀之后开始
    batch.logits[i] = (i == incremental_count - 1);
}
llama_decode(ctx, batch);  // ★ 只decode增量token，复用已有KV Cache
```

### 性能对比

| 场景 | Server-12 | Server-13 | 提升 |
|------|-----------|-----------|------|
| 首轮对话 | 31.24s | 31.56s | 持平 |
| 第2轮对话 | 15.82s | **0.42s** | **37.6x** |
| 第5轮对话 | ~30s | **0.5s** | **~60x** |

> 对话轮数越多，性能优势越明显。因为 Server-12 需要重新处理所有历史 token，而 Server-13 只处理每轮新增的 token。

### Server-13 的局限性

1. **所有会话共享一个模型，但每个会话独占一个 context** → 内存随会话数线性增长
2. **无 LRU 淘汰机制** → 会话越多内存越大，无法限制
3. **无持久化** → 重启后所有KV Cache丢失
4. **无用户隔离** → 任何人可以访问任何会话

---

## Server-14：生产级系统

### 核心改进

Server-14 是一次**架构级升级**，从"Demo级别"升级为"接近生产级"的系统：

| 改进项 | 说明 |
|--------|------|
| JWT认证 | 用户注册/登录/Token验证 |
| SQLite持久化 | 会话和消息持久化到数据库 |
| SessionContextPool | KV Cache 池 + LRU淘汰（限制内存） |
| 流式输出(SSE) | 逐token推送，用户体验大幅提升 |
| DeepSeek-R1支持 | 分离思考过程和最终回答 |
| 用户隔离 | 每个用户只能访问自己的会话 |
| DB-内存同步 | 懒加载机制，按需从数据库恢复会话 |
| 批处理推理(可选) | 多请求并行推理 |

### 文件结构

```
server-14/
├── CMakeLists.txt                    # 构建配置
├── include/
│   ├── JWTAuth.h                     # ★ 293行 - JWT认证（HMAC-SHA256）
│   ├── AuthMiddleware.h              # ★ 101行 - HTTP认证中间件
│   ├── ModelManagerV2.h              # ★ 246行 - 增强版模型管理器
│   ├── SessionContextPool.h          # ★ 165行 - KV Cache池（LRU淘汰）
│   ├── SessionManager.h              # ★ 630行 - 会话管理（DB持久化）
│   ├── Database.h                    # ★ 290行 - SQLite封装（扩展）
│   ├── Router.h                      # ★ 638行 - 路由（JWT保护）
│   ├── BatchInferenceEngine.h        # ★ 168行 - 批处理引擎
│   ├── HttpServer.h                  # HTTP服务器（继承）
│   ├── HttpRequest.h                 # 请求解析（继承）
│   ├── HttpResponse.h                # 响应构建（继承）
│   ├── ThreadPool.h                  # 线程池（继承）
│   ├── Logger.h                      # 日志工具
│   └── FileUtils.h                   # 文件工具
├── src/
│   ├── main_v2.cpp                   # ★ 93行 - 主入口
│   ├── ModelManagerV2.cpp            # ★ 571行 - 推理引擎实现
│   ├── SessionContextPool.cpp        # ★ 292行 - Context池实现
│   └── BatchInferenceEngine.cpp      # ★ 338行 - 批处理实现
└── UI/
    ├── login.html                    # ★ 登录页面
    ├── register.html                 # ★ 注册页面
    ├── multichat.html                # 聊天界面（扩展：JWT + 流式 + 思考过程）
    └── multichat.css                 # 样式（扩展：思考过程折叠样式）
```

### 核心模块详解

#### 1. JWTAuth.h — JWT 认证系统 (`JWTAuth.h:1-293`)

手工实现的 JWT（JSON Web Token）认证，不依赖第三方库。

**Token 格式**：`base64url(header).base64url(payload).base64url(signature)`

```cpp
class JWTAuth {
public:
    // 生成Token（24小时有效期）
    static std::string generateToken(const std::string& username) {
        // Header: {"alg":"HS256","typ":"JWT"}
        std::string header = base64UrlEncode("{\"alg\":\"HS256\",\"typ\":\"JWT\"}");

        // Payload: {"username":"xxx", "exp":timestamp+86400}
        long long exp = std::time(nullptr) + 86400;  // 24小时
        std::string payload = base64UrlEncode(
            "{\"username\":\"" + username + "\",\"exp\":" + std::to_string(exp) + "}");

        // Signature: HMAC-SHA256(header.payload, secret_key)
        std::string signature = base64UrlEncode(
            hmacSHA256(header + "." + payload, secret_key_));

        return header + "." + payload + "." + signature;
    }

    // 验证Token（返回用户名，失败返回空字符串）
    static std::string validateToken(const std::string& token) {
        // 1. 分割 header.payload.signature
        // 2. 重新计算签名，与提供的签名比较
        // 3. 检查是否过期（exp < current_time）
        // 4. 提取并返回 username
    }

private:
    static std::string hmacSHA256(data, key);  // 使用 OpenSSL
    static std::string base64UrlEncode(input);
    static std::string base64UrlDecode(input);
    static std::string secret_key_;
};
```

#### 2. AuthMiddleware.h — 认证中间件 (`AuthMiddleware.h:1-101`)

从 HTTP 请求中提取和验证 JWT Token。

```cpp
class AuthMiddleware {
public:
    // 从 Authorization: Bearer <token> 头中提取token
    static std::string extractToken(const HttpRequest& req) {
        std::string auth = req.getHeader("Authorization");
        if (auth.substr(0, 7) == "Bearer ") return auth.substr(7);
        return "";
    }

    // 验证请求，返回用户名
    static std::string authenticate(const HttpRequest& req) {
        std::string token = extractToken(req);
        if (token.empty()) return "";
        return JWTAuth::validateToken(token);
    }

    static HttpResponse makeUnauthorizedResponse(msg);   // 401
    static HttpResponse makeForbiddenResponse(msg);       // 403
};
```

#### 3. Database.h — SQLite 持久化（大幅扩展） (`Database.h:1-290`)

**数据库 Schema：**

```sql
-- 用户表
CREATE TABLE users (
    username TEXT PRIMARY KEY,
    password TEXT
);

-- 会话表（Server-14 新增 session_id 字段）
CREATE TABLE chats (
    chat_id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT,
    title TEXT DEFAULT '',
    session_id TEXT DEFAULT '',    -- ★ 关联 SessionManager 的 session_id
    created DATETIME DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_session_id ON chats(session_id);

-- 消息表
CREATE TABLE messages (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chat_id INTEGER,
    role TEXT,          -- "user" | "assistant"
    content TEXT,
    ts DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

**Server-14 新增的数据库方法：**

```cpp
class Database {
public:
    // ===== 原有方法（Server-11） =====
    bool registerUser(username, password);
    bool loginUser(username, password);
    int createChat(username);                        // 返回 chat_id
    bool addMessage(chatId, role, content);
    std::vector<int> listChats(username);
    std::vector<std::pair<std::string,std::string>> getMessages(chatId);

    // ===== Server-14 新增方法 =====
    void linkSessionToChat(session_id, chat_id);     // session_id ↔ chat_id 关联
    int getChatIdForSession(session_id);              // session_id → chat_id
    std::vector<ChatInfo> loadUserChats(username);    // 加载用户所有会话信息
    void updateChatTitle(chat_id, new_title);         // 更新标题
    void deleteChat(chat_id);                         // 删除会话+消息
    bool verifyChatOwnership(chat_id, username);      // 验证所有权
};

// 会话信息结构体
struct ChatInfo {
    int chat_id;
    std::string session_id, username, title, created;
};
```

#### 4. SessionManager.h — 会话管理（DB-内存同步）(`SessionManager.h:1-630`)

相比 Server-12 的纯内存会话管理，Server-14 实现了**数据库持久化 + 内存缓存**的混合架构。

**核心改进点：**

```cpp
class SessionManager {
public:
    SessionManager(Database& db);  // ★ 接受数据库引用

    // ===== 基本操作（增强版）=====
    std::string createSession(const std::string& username);  // ★ 关联用户
    bool hasSession(session_id);              // ★ 检查内存 OR 数据库
    void addUserMessage(session_id, content);  // ★ 同时写内存和数据库
    void addAssistantMessage(session_id, content);

    // ===== Server-14 新增方法 =====
    void loadUserSessions(username);                     // 从DB加载用户所有会话
    std::string getAllSessionsForUser(username);          // 仅返回该用户的会话
    bool verifySessionOwnership(session_id, username);   // 验证会话归属

private:
    Database& db_;                                        // ★ 数据库引用
    std::unordered_map<std::string, Session> sessions_;   // 内存缓存
    std::unordered_map<std::string, int> sessionToChatId_; // session_id → chat_id
    std::unordered_map<int, std::string> chatIdToUser_;    // chat_id → username

    // ★ 核心机制：懒加载
    void ensureSessionLoadedLocked(const std::string& session_id);
};
```

**ensureSessionLoadedLocked() — DB-内存同步核心** (`SessionManager.h:585-611`)

这是 Server-14 最关键的内部方法，实现了按需从数据库恢复会话到内存的懒加载机制：

```cpp
void ensureSessionLoadedLocked(const std::string& session_id) {
    // 已在内存中 → 直接返回
    if (sessions_.find(session_id) != sessions_.end()) return;

    // 从数据库查找
    int chat_id = db_.getChatIdForSession(session_id);
    if (chat_id < 0) return;  // 数据库中也不存在

    // 从数据库重建 Session 对象
    Session sess(session_id);
    auto messages = db_.getMessages(chat_id);
    for (const auto& [role, content] : messages) {
        sess.addMessage(role, content);  // 恢复所有历史消息
    }

    // 放入内存缓存
    sessions_[session_id] = sess;
    sessionToChatId_[session_id] = chat_id;
}
```

> 这个方法在 `addUserMessage`、`addAssistantMessage`、`getFullContext`、`getRecentContext` 等所有操作前被调用，确保会话数据在使用前已加载到内存。

#### 5. SessionContextPool.h/.cpp — KV Cache 池（LRU淘汰）

这是 Server-14 对 Server-13 的重大架构升级。Server-13 为每个会话保留一个 context（无限增长），Server-14 引入了**固定大小的对象池**和 **LRU 淘汰**。

**数据结构** (`SessionContextPool.h:32-54`)：
```cpp
struct SessionContext {
    llama_context* ctx;                 // 专属context（包含KV Cache）
    std::vector<llama_token> tokens;    // 已处理的完整token序列
    int cached_token_count;             // KV Cache中的token数
    long long last_used;                // LRU时间戳

    void updateLastUsed();              // 更新访问时间
};

class SessionContextPool {
public:
    SessionContextPool(llama_model* model, int max_sessions, int n_ctx, int n_threads);

    SessionContext* getOrCreateSession(session_id);  // 获取或创建
    bool processIncrementalTokens(session_id, new_tokens, prompt);  // ★ 增量推理
    void deleteSession(session_id);
    void evictLRU();                                 // LRU淘汰

private:
    std::unordered_map<std::string, SessionContext> session_map_;
    int max_sessions_;           // 池容量上限
    uint64_t total_hits_;        // 缓存命中次数
    uint64_t total_misses_;      // 缓存未命中次数
    uint64_t total_evictions_;   // 淘汰次数
};
```

**getOrCreateSession() — 池管理** (`SessionContextPool.cpp:91-132`)：
```cpp
SessionContext* getOrCreateSession(const std::string& session_id) {
    // 缓存命中 → 返回已有context，更新LRU时间
    auto it = session_map_.find(session_id);
    if (it != session_map_.end()) {
        it->second.updateLastUsed();
        total_hits_++;
        return &it->second;
    }

    // 缓存未命中
    total_misses_++;

    // 如果已满 → LRU淘汰最久未使用的session
    if (session_map_.size() >= max_sessions_) {
        evictLRU();
    }

    // 创建新的context
    SessionContext new_ctx;
    new_ctx.ctx = createContext();  // llama_init_from_model(model_, params)
    new_ctx.cached_token_count = 0;
    new_ctx.updateLastUsed();
    total_creates_++;

    session_map_[session_id] = std::move(new_ctx);
    return &session_map_[session_id];
}
```

**evictLRU() — 淘汰策略** (`SessionContextPool.cpp:64-89`)：
```cpp
void evictLRU() {
    // 找到 last_used 最小（最久未使用）的session
    auto oldest = session_map_.begin();
    for (auto it = session_map_.begin(); it != session_map_.end(); ++it) {
        if (it->second.last_used < oldest->second.last_used)
            oldest = it;
    }

    // 释放其context并从池中移除
    llama_free(oldest->second.ctx);
    session_map_.erase(oldest);
    total_evictions_++;
}
```

**processIncrementalTokens() — 增量推理核心** (`SessionContextPool.cpp:134-248`)

这是 Server-14 中最关键的算法，相比 Server-13 增加了**KV Cache 不一致处理**：

```
Phase 1: 公共前缀检测 (Lines 160-169)
  逐token比较 old_tokens 和 new_tokens，找到最长匹配前缀

Phase 2: KV Cache 不一致处理 (Lines 186-218)
  if common_prefix_len != cached_token_count:
    情况1: Cache太多（用户编辑了历史）→ 重建context，重新处理所有token
    情况2: Cache太少（上次推理中断）  → 从cache位置继续

Phase 3: 批量处理增量token (Lines 220-242)
  创建 batch，只包含 new_tokens[common_prefix_len:] 的token
  设置正确的绝对位置编码
  只有最后一个token需要logits
  调用 llama_decode(ctx, batch)

Phase 4: 状态更新 (Lines 243-245)
  session_ctx.tokens = new_tokens
  session_ctx.cached_token_count = new_tokens.size()
```

#### 6. ModelManagerV2.h/.cpp — 增强版推理引擎

相比 Server-12/13 的 `ModelManager`，`ModelManagerV2` 新增了流式输出、Chat Template、批处理支持。

```cpp
class ModelManagerV2 {
public:
    static ModelManagerV2& instance();

    bool loadModel(path, n_ctx, n_threads, max_sessions, enable_batch, batch_size);

    // ★ 带KV缓存的推理（基础版）
    std::string inferWithCache(session_id, prompt, tokens, max_tokens, temperature);
    std::string inferWithCache(session_id, prompt, max_tokens, temperature);

    // ★ 流式推理（逐token回调）
    std::string inferWithCacheStreaming(session_id, prompt, max_tokens, temperature,
                                        StreamCallback callback);

    // ★ 应用模型内置Chat Template
    std::string applyChatTemplate(
        const std::vector<std::pair<std::string,std::string>>& messages,
        bool add_generation_prompt);

    // 批处理推理（异步）
    std::future<std::string> inferBatch(session_id, prompt, max_tokens, temperature);

private:
    std::unique_ptr<SessionContextPool> session_pool_;    // ★ KV Cache池
    std::unique_ptr<BatchInferenceEngine> batch_engine_;  // 批处理引擎

    // 核心生成方法
    std::string generateTokens(ctx, max_tokens, temperature, n_past);
    std::string generateTokensStreaming(ctx, max_tokens, temperature, n_past, callback);
};
```

**inferWithCache() 完整流程** (`ModelManagerV2.cpp:163-210`)：
```cpp
std::string inferWithCache(session_id, prompt, tokens, max_tokens, temperature) {
    // 1. 获取或创建session context（触发LRU淘汰）
    auto* session_ctx = session_pool_->getOrCreateSession(session_id);

    // 2. 增量处理prompt token（找公共前缀，只处理新增部分）
    session_pool_->processIncrementalTokens(session_id, tokens, prompt);

    // 3. 从KV Cache位置开始生成token
    std::string result = generateTokens(
        session_ctx->ctx, max_tokens, temperature,
        session_ctx->cached_token_count);  // ★ n_past = cached位置

    // 4. 将生成的token也加入缓存
    auto generated_tokens = tokenize(result, false);
    auto all_tokens = tokens;
    all_tokens.insert(all_tokens.end(), generated_tokens.begin(), generated_tokens.end());
    session_ctx->tokens = all_tokens;
    session_ctx->cached_token_count = all_tokens.size();

    return result;
}
```

**generateTokensStreaming() — 流式生成** (`ModelManagerV2.cpp:380-449`)：
```cpp
std::string generateTokensStreaming(ctx, max_tokens, temperature, n_past, callback) {
    std::string result;
    for (int i = 0; i < max_tokens; i++) {
        // 1. 获取logits → 贪心采样
        float* logits = llama_get_logits(ctx);
        llama_token token = argmax(logits);

        // 2. 检查停止条件
        if (is_eog(token) || is_stop_sequence(token_text)) break;

        // 3. ★ 调用回调函数，逐token推送给前端
        std::string piece = token_to_text(token);
        if (!callback(piece)) break;  // 回调返回false → 停止生成

        result += piece;

        // 4. decode下一个token
        decode_single_token(ctx, token, n_past++);
    }
    return result;
}
```

**applyChatTemplate() — 使用模型内置模板** (`ModelManagerV2.cpp:505-571`)：
```cpp
std::string applyChatTemplate(
    const std::vector<std::pair<std::string,std::string>>& messages,
    bool add_generation_prompt)
{
    // 转换为 llama_chat_message 格式
    std::vector<llama_chat_message> chat_messages;
    for (auto& [role, content] : messages) {
        chat_messages.push_back({role.c_str(), content.c_str()});
    }

    // 获取模型内置模板名
    const char* tmpl = llama_model_chat_template(model_, nullptr, nullptr, 0);

    // 调用 llama.cpp API 渲染（两次调用：先获取长度，再填充）
    int len = llama_chat_apply_template(nullptr, tmpl,
        chat_messages.data(), chat_messages.size(),
        add_generation_prompt, buf, buf_size);

    return std::string(buf, len);
}
```

> **与 Server-12 的对比**：Server-12 使用手写的 ChatML 格式（`<|user|>\n...\n<|assistant|>\n`），Server-14 使用模型自带的 chat template，自动适配不同模型格式（ChatML、DeepSeek、Llama 等）。

#### 7. Router.h — 路由系统（JWT保护 + 流式输出 + 思考过程分离）

**DeepSeek-R1 思考过程分离** (`Router.h:153-193`)：
```cpp
struct ThinkingAndAnswer {
    std::string thinking;    // <think>...</think> 之间的内容
    std::string answer;      // 思考标签之外的回答
    bool has_thinking;
};

ThinkingAndAnswer separateThinkingAndAnswer(const std::string& text) {
    // 查找 <think>...</think> 标签
    auto start = text.find("<think>");
    auto end = text.find("</think>");
    if (start != npos && end != npos) {
        return {text.substr(start+7, end-start-7),  // thinking
                text.substr(end+8),                   // answer
                true};
    }
    return {"", text, false};
}
```

**流式聊天端点** (`Router.h:425-539`)：
```
POST /api/sessions/{id}/chat/stream

流程:
1. AuthMiddleware::authenticate(req) → 获取username
2. verifySessionOwnership(session_id, username) → 验证权限
3. sm.addUserMessage(session_id, message)
4. context = sm.getRecentContext(session_id, 20, &truncated)
5. if truncated → mm.deleteSession(session_id)  // 清空KV Cache
6. mm.inferWithCacheStreaming(session_id, context, max_tokens, temp, callback)
   callback 内部:
   - 检测 <think> 和 </think> 标签
   - 发送 SSE 事件: data: {"token":"...", "type":"thinking"} 或 {"type":"answer"}
7. 完成后发送: data: {"done":true, "message":"最终回答", "thinking":"思考过程"}
```

**Server-14 完整 API 列表：**

| 接口 | 方法 | 认证 | 功能 |
|------|------|------|------|
| `/api/auth/register` | POST | 无 | 用户注册 |
| `/api/auth/login` | POST | 无 | 登录（返回JWT Token） |
| `/api/sessions/new` | POST | Bearer Token | 创建新会话 |
| `/api/sessions` | GET | Bearer Token | 获取当前用户的会话列表 |
| `/api/sessions/{id}/history` | GET | Bearer Token | 获取会话历史 |
| `/api/sessions/{id}/chat` | POST | Bearer Token | 发送消息（同步响应） |
| `/api/sessions/{id}/chat/stream` | POST | Bearer Token | 发送消息（SSE流式响应） |
| `/api/sessions/{id}` | DELETE | Bearer Token | 删除会话 |
| `/api/sessions/{id}/title` | PUT | Bearer Token | 修改标题 |

#### 8. BatchInferenceEngine.h/.cpp — 批处理推理引擎

利用 llama.cpp 的多序列支持，在单个 `llama_decode()` 调用中并行处理多个推理请求。

```cpp
class BatchInferenceEngine {
public:
    BatchInferenceEngine(int batch_size, int batch_timeout_ms);

    // 提交异步推理请求
    std::future<std::string> submitRequest(session_id, prompt, max_tokens, temperature);

    void start();  // 启动工作线程
    void stop();   // 停止

private:
    void workerLoop();           // 收集请求，达到batch_size或超时后处理
    void processBatch(requests); // ★ 核心：多序列并行推理
};
```

**processBatch() 核心算法** (`BatchInferenceEngine.cpp:111-321`)：

```
Phase 1: 并行Prompt处理
  - 将所有请求的prompt token放入同一个batch
  - 每个请求分配不同的 seq_id
  - 单次 llama_decode() 处理所有prompt

Phase 2: 并行Token生成
  loop:
    - 为每个未完成的序列采样下一个token
    - 检查各序列的停止条件
    - 将所有新token放入batch
    - 单次 llama_decode() 处理所有序列

关键优化：一次decode调用处理N个序列，GPU/CPU利用率更高
```

#### 9. main_v2.cpp — 服务器初始化 (`main_v2.cpp:1-93`)

```cpp
int main() {
    // 1. 初始化 JWT 密钥
    JWTAuth::setSecretKey("server14-secret-key-change-in-production-abc123");

    // 2. 打开数据库
    Database db("users.db");

    // 3. 加载模型
    const int n_ctx = 2048;
    const int n_threads = 4;
    const int max_sessions = 50;     // KV Cache池容量
    ModelManagerV2::instance().loadModel(model_path, n_ctx, n_threads,
                                         max_sessions, false, 8);

    // 4. 创建会话管理器（关联数据库）
    SessionManager sessionManager(db);

    // 5. 创建HTTP服务器并启动
    HttpServer server(8080, 10, db);
    server.start(sessionManager);
}
```

#### 10. 前端扩展

**login.html / register.html** — 用户认证页面
- 表单提交到 `/api/auth/login` 和 `/api/auth/register`
- 登录成功后将 JWT Token 存入 `localStorage`

**multichat.html 扩展**：
- 所有 API 请求携带 `Authorization: Bearer <token>` 头
- 使用 `EventSource` / `fetch().body.getReader()` 接收 SSE 流式响应
- 实时显示思考过程（可折叠 `<think>...</think>` 内容）
- 登出功能：清除 localStorage 中的 token

**multichat.css 扩展**：
```css
/* 思考过程折叠样式 */
.thinking-section { border: 1px solid #565869; border-radius: 6px; }
.thinking-toggle { background-color: #2a2b32; cursor: pointer; }
.thinking-content { background-color: #1e1f23; max-height: 300px; overflow-y: auto; }
```

---

## 版本间核心差异对比

### Context 生命周期对比

```
Server-11: generate()内新建 → 推理 → 立即释放   （单轮，无状态）
Server-12: infer()内新建   → 推理 → 立即释放   （多轮历史但无缓存）
Server-13: 首次创建       → 推理 → 保留不释放   （持久化，无限增长）
Server-14: 按需创建       → 推理 → 池化管理     （LRU淘汰，固定内存）
```

### Prompt 构造方式对比

```
Server-11: 直接使用 llama_chat_apply_template（或原始文本）
Server-12: ChatSession.makePrompt() → 手写 ChatML 格式
           <|user|>\n...\n<|assistant|>\n
Server-13: 同 Server-12
Server-14: ModelManagerV2.applyChatTemplate() → 使用模型内置模板
           自动适配 ChatML / DeepSeek / Llama 等格式
```

### 会话存储对比

```
Server-11: 无会话概念
Server-12: 纯内存 map（SessionManager.sessions_）→ 重启丢失
Server-13: 纯内存 map + KV Cache（KVCachedSession）→ 重启丢失
Server-14: SQLite持久化 + 内存缓存（ensureSessionLoadedLocked 懒加载）
           → 重启后从DB恢复（KV Cache需重建，但历史不丢失）
```

### 推理流程对比

```
Server-12 infer():
  session.add("user", msg)
  prompt = session.makePrompt()    // 全部历史 → ChatML
  tokens = tokenize(prompt)        // 全部token
  ctx = 新建()
  decode(ctx, 全部tokens)          // ★ 全量处理
  result = generate(ctx)
  free(ctx)                        // ★ 销毁

Server-13 infer():
  session.add("user", msg)
  prompt = session.makePrompt()
  new_tokens = tokenize(prompt)
  ctx = 已有 or 新建()
  incremental = diff(cached_tokens, new_tokens)  // ★ 计算增量
  decode(ctx, incremental)                         // ★ 只处理增量
  result = generate(ctx, n_past)
  update(cached_tokens, n_past)                    // 更新缓存状态

Server-14 inferWithCache():
  tokens = tokenize(prompt)
  session_ctx = pool.getOrCreateSession(id)        // ★ 从池中获取
  if pool满 → evictLRU()                           // ★ LRU淘汰
  pool.processIncrementalTokens(id, tokens)        // ★ 增量推理 + 不一致处理
  result = generateTokens(ctx, n_past)             // 从缓存位置生成
  或 generateTokensStreaming(ctx, n_past, callback) // ★ 流式生成
```

---

## 历史遗留代码清理记录

### 已清理的遗留文件

Server-14 开发过程中产生了一些遗留代码，现已全部清理完毕：

| 已删除文件 | 原行数 | 说明 |
|------------|--------|------|
| `src/inference/ModelManager.h` | 314 | 旧版模型管理器，已被 ModelManagerV2 替代 |
| `src/inference/ModelManager.cpp` | ~400 | 旧版推理实现 |
| `src/inference/ModelManager_new.h` | ~300 | 过渡文件，从未完成 |
| `include/ContextPool.h` | 142 | 通用Context池，实际使用 SessionContextPool |
| `src/ContextPool.cpp` | 153 | 通用池实现 |

### 当前 Server-14 实际使用的组件

```bash
✅ ModelManagerV2.h      - Router.h 引用，增强版推理引擎
✅ SessionManager.h      - Router.h 引用，会话管理（DB持久化）
✅ JWTAuth.h             - Router.h 引用，JWT认证
✅ AuthMiddleware.h      - Router.h 引用，认证中间件
✅ SessionContextPool.h  - ModelManagerV2.h 引用，KV Cache池
✅ Database.h            - SessionManager.h 引用，SQLite封装
✅ BatchInferenceEngine.h - ModelManagerV2.h 引用，批处理引擎
```

### 清理效果

| 指标 | 清理前 | 清理后 |
|------|--------|--------|
| 文件数 | 27 | 22 |
| 代码行数 | ~5,883 | ~4,500 |

---

## 附录：关键代码位置索引

### Server-11 核心
- `SimpleInference.h:44-93` — loadModel() 模型加载
- `SimpleInference.h:110-191` — generate() 单轮推理入口（5步流程）
- `SimpleInference.h:205-280` — applyChatTemplate() 聊天模板
- `SimpleInference.h:330-352` — process_prompt() Prefill阶段
- `SimpleInference.h:368-457` — generate_tokens() 自回归解码循环

### Server-12 核心
- `ModelManager.h:44-149` — ChatSession 类（add/makePrompt/prune）
- `ModelManager.h:83-93` — makePrompt() ChatML格式构造
- `ModelManager.h:128-148` — prune() 历史裁剪（4轮限制 + 摘要）
- `ModelManager.h:172-313` — ModelManager 单例（infer 5步流程）
- `SessionManager.h:51-143` — Session 结构体
- `SessionManager.h:166-428` — SessionManager 类

### Server-13 核心
- `ModelManager.h:61-64` — add() 返回bool（标记是否需清缓存）
- `ModelManager.h:131-155` — prune() 返回bool
- `ModelManager.h:198-254` — KVCachedSession 结构体（ctx + n_past + cached_tokens）
- `ModelManager.cpp:63-69` — KVCachedSession 析构函数（释放context）
- `ModelManager.cpp:~180-240` — 增量推理算法（公共前缀检测 + 增量decode）

### Server-14 核心
- `JWTAuth.h:26-49` — generateToken()（HMAC-SHA256签名）
- `JWTAuth.h:56-98` — validateToken()（签名验证 + 过期检查）
- `AuthMiddleware.h:23-49` — extractToken()（从Header提取）
- `Database.h:17-54` — SQLite Schema（users/chats/messages）
- `Database.h:164-191` — linkSessionToChat() / getChatIdForSession()
- `SessionManager.h:585-611` — ensureSessionLoadedLocked()（DB-内存懒加载）
- `SessionManager.h:540-558` — verifySessionOwnership()（权限验证）
- `SessionContextPool.cpp:64-89` — evictLRU()（LRU淘汰）
- `SessionContextPool.cpp:91-132` — getOrCreateSession()（池管理）
- `SessionContextPool.cpp:134-248` — processIncrementalTokens()（增量推理 + 不一致处理）
- `ModelManagerV2.cpp:163-210` — inferWithCache()（完整推理流程）
- `ModelManagerV2.cpp:380-449` — generateTokensStreaming()（流式生成）
- `ModelManagerV2.cpp:505-571` — applyChatTemplate()（模型内置模板）
- `Router.h:160-193` — separateThinkingAndAnswer()（DeepSeek-R1思考分离）
- `Router.h:425-539` — sessionChatStreamHandler（SSE流式端点）
- `BatchInferenceEngine.cpp:111-321` — processBatch()（多序列并行推理）
