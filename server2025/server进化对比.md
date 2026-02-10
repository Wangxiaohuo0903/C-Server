# Server 版本演进详细对比文档

本文档详细介绍了从 Server-9 到 Server-14 的完整演进过程，包括每个版本的核心改进、代码变化和技术原理。

---

## 目录

1. [版本演进路线图](#版本演进路线图)
2. [Server-9 → Server-11：引入大语言模型](#server-9--server-11引入大语言模型)
3. [Server-11 → Server-12：多轮对话支持](#server-11--server-12多轮对话支持)
4. [Server-12 → Server-13：KV Cache 性能优化](#server-12--server-13kv-cache-性能优化)
5. [Server-13 → Server-14：生产级认证系统](#server-13--server-14生产级认证系统)
6. [技术栈对比总结](#技术栈对比总结)

---

## 版本演进路线图

```
Server-9 (Nginx反向代理)
    │
    │ 新增: SimpleInference.h (llama.cpp封装)
    │ 新增: /infer-simple API
    ▼
Server-11 (单轮AI推理)
    │
    │ 新增: SessionManager.h (多会话管理)
    │ 新增: ModelManager.h (模型管理器)
    │ 新增: ChatGPT风格UI
    ▼
Server-12 (多轮对话)
    │
    │ 升级: KVCachedSession (KV Cache持久化)
    │ 新增: 增量推理逻辑
    │ 性能: 5-10倍提升
    ▼
Server-13 (KV Cache优化)
    │
    │ 新增: JWTAuth.h (JWT认证)
    │ 新增: ModelManagerV2.h (增强版模型管理)
    │ 新增: SessionContextPool.h (Context对象池)
    │ 新增: 数据库持久化
    ▼
Server-14 (生产级认证系统)
```

---

## Server-9 → Server-11：引入大语言模型

### 1. 核心差异概览

| 维度 | Server-9 (Web服务框架) | Server-11 (AI推理服务) |
|------|----------------------|----------------------|
| 核心功能 | 用户注册、登录、HTTP路由 | **单轮AI对话** + 用户管理 |
| 计算类型 | IO密集型 (网络/数据库) | **计算密集型 + IO密集型** |
| 外部依赖 | SQLite | SQLite + **llama.cpp** + **GGUF模型** |
| 关键新组件 | - | **SimpleInference.h** |

### 2. 新增核心组件：SimpleInference.h

**文件位置**: `server-11-LLM/SimpleInference.h`

这是 Server-11 的核心新增，封装了所有与 llama.cpp 交互的逻辑。

```cpp
// server-11-LLM/SimpleInference.h:22-33
class SimpleInference {
public:
    SimpleInference() : model_(nullptr) {}
    ~SimpleInference() {
        if (model_) {
            llama_model_free(model_);
        }
    }

    // 核心接口
    bool loadModel(const std::string& model_path);           // 加载GGUF模型
    std::string generate(const std::string& prompt, ...);    // 文本生成
    // ...
};
```

**关键方法说明**:

| 方法 | 位置 | 功能 |
|------|------|------|
| `loadModel()` | 第41-84行 | 加载GGUF格式模型，配置CPU/GPU参数 |
| `generate()` | 第101-179行 | 单轮文本生成，包含完整推理流程 |
| `applyChatTemplate()` | 第197-264行 | 使用模型内置模板格式化对话 |
| `tokenize()` | 第278-315行 | 文本转Token序列 |
| `process_prompt()` | 第330-352行 | Prefill阶段，建立KV Cache |
| `generate_tokens()` | 第368-457行 | 自回归解码循环 |

### 3. main.cpp 的变化

**Server-9 main.cpp** (简单的Web启动):
```cpp
// server-9-Nginx/main.cpp:5-14
int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);
    Database db("users.db");
    HttpServer server(port, 10, db);
    server.setupRoutes();
    server.start();
    return 0;
}
```

**Server-11 main.cpp** (新增模型加载流程):
```cpp
// server-11-LLM/main.cpp:34-116
int main(int argc, char* argv[]) {
    // ★ 新增步骤1: 初始化推理引擎
    SimpleInference inference;

    // ★ 新增步骤2: 获取模型路径（环境变量）
    const char* model_path_env = std::getenv("MODEL_PATH");
    std::string model_path = model_path_env ? model_path_env : "../models/tinyllama-q4.gguf";

    // ★ 新增步骤3: 加载模型
    if (!inference.loadModel(model_path)) {
        std::cerr << "Failed to load model!\n";
        return 1;
    }

    // 原有逻辑
    Database db("users.db");
    HttpServer server(port, 10, db);

    // ★ 关键变化: 传递inference对象给路由
    server.setupRESTfulRoutes(inference);
    server.setupRoutes();
    server.start();
    return 0;
}
```

### 4. 新增API接口

**新增接口**: `POST /infer-simple`

```cpp
// Router.h 中的路由设置
router.addRoute("POST", "/infer-simple",
    [&inference](const HttpRequest& req) {
        auto js = req.parseJson();
        std::string prompt = js["prompt"];
        std::string answer = inference.generate(prompt);
        return HttpResponse::makeJsonResponse(answer);
    });
```

**请求格式**:
```json
{
    "prompt": "你好，请介绍一下自己",
    "max_tokens": 64
}
```

**响应格式**:
```json
{
    "success": true,
    "response": "我是一个AI助手..."
}
```

### 5. 技术原理

**LLM推理五步流程**（在 `SimpleInference::generate()` 中实现）:

1. **Prompt Engineering** (第112-124行): 使用 `applyChatTemplate()` 格式化输入
2. **Tokenization** (第149-155行): 调用 `tokenize()` 将文本转为Token序列
3. **Prefill** (第162-165行): 调用 `process_prompt()` 建立KV Cache
4. **Decoding** (第171行): 调用 `generate_tokens()` 自回归生成
5. **Cleanup** (第176行): 释放临时context

---

## Server-11 → Server-12：多轮对话支持

### 1. 核心差异概览

| 维度 | Server-11 (单轮推理) | Server-12 (多轮对话) |
|------|---------------------|---------------------|
| 对话模式 | 单轮（无历史记录） | **多轮（记住上下文）** |
| 会话管理 | 无 | **SessionManager** |
| 推理引擎 | SimpleInference | **ModelManager** |
| UI界面 | 简单表单 | **ChatGPT风格侧边栏** |

### 2. 新增核心组件

#### 2.1 SessionManager.h - 会话管理器

**文件位置**: `server-12-MultiChat/include/SessionManager.h`

```cpp
// server-12-MultiChat/include/SessionManager.h:64-75
struct Message {
    std::string role;       // "user" or "assistant"
    std::string content;    // 消息内容
    long long timestamp;    // 时间戳（毫秒）
};

// 第80-161行
struct Session {
    std::string session_id;
    std::vector<Message> history;      // 对话历史
    long long created_at;              // 创建时间
    long long last_active;             // 最后活跃时间
    std::string title;                 // 会话标题

    void addMessage(const std::string& role, const std::string& content);
    std::string getFullContext() const;
    std::string getRecentContext(int n_turns = 5) const;
};
```

**SessionManager 核心方法** (第166-428行):

| 方法 | 行号 | 功能 |
|------|------|------|
| `createSession()` | 174-182 | 创建新会话，返回唯一session_id |
| `addUserMessage()` | 195-204 | 添加用户消息到指定会话 |
| `addAssistantMessage()` | 209-218 | 添加AI回复到指定会话 |
| `getFullContext()` | 223-231 | 获取完整对话历史 |
| `getRecentContext()` | 238-246 | 获取最近N轮对话上下文 |
| `getAllSessions()` | 290-320 | 获取所有会话列表（JSON格式） |
| `getSessionHistory()` | 326-347 | 获取指定会话的历史记录 |
| `deleteSession()` | 352-357 | 删除会话 |

#### 2.2 ModelManager.h - 模型管理器

**文件位置**: `server-12-MultiChat/src/inference/ModelManager.h`

```cpp
// server-12-MultiChat/src/inference/ModelManager.h:44-148
class ChatSession {
public:
    void add(const std::string& role, const std::string& content) {
        history.push_back({role, content});
        prune();   // 自动裁剪历史
    }

    std::string makePrompt() const {
        std::ostringstream oss;
        for (const auto& m : history) {
            oss << "<|" << m.role << "|>\n" << m.content << "\n";
        }
        oss << "<|assistant|>\n";
        return oss.str();
    }

private:
    void prune() {
        // 保留最近4轮对话，避免prompt过长
        const int max_round_keep = 4;
        // ...
    }
};
```

**ModelManager 单例模式** (第172-313行):

```cpp
// server-12-MultiChat/src/inference/ModelManager.h:172-313
class ModelManager {
public:
    static ModelManager& instance();  // 单例获取

    bool loadModel(const std::string& path, int n_ctx, int n_threads);

    // ★ 核心接口：多轮对话推理
    std::string infer(const std::string& chat_id,
                      const std::string& user_msg,
                      int maxTokens, float temperature);

    // 公开的原始推理接口
    std::string raw_infer(const std::string& prompt, int maxTokens, float temperature) const;

private:
    std::unordered_map<std::string, ChatSession> chat_sessions_;  // 会话字典
};
```

### 3. main.cpp 的变化

```cpp
// server-12-MultiChat/src/main.cpp:17-56
int main() {
    // 1. 初始化数据库
    Database db("users.db");

    // 2. 加载模型（使用单例模式）
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath : "../models/smollm-360m-q4.gguf";

    if (!ModelManager::instance().loadModel(modelPath, 2048, 4)) {
        return 1;
    }

    // ★ 新增: 创建SessionManager
    static SessionManager sessionManager;

    // 4. 创建HttpServer并传入sessionManager
    HttpServer server(8080, 10, db);
    server.start(sessionManager);  // ★ 启动方式变化

    return 0;
}
```

### 4. 新增API接口

| 接口 | 方法 | 功能 |
|------|------|------|
| `/api/sessions/new` | POST | 创建新会话 |
| `/api/sessions` | GET | 获取所有会话列表 |
| `/api/sessions/{id}/history` | GET | 获取会话历史 |
| `/api/sessions/{id}/chat` | POST | 发送消息并获取回复 |
| `/api/sessions/{id}/title` | PUT | 修改会话标题 |
| `/api/sessions/{id}` | DELETE | 删除会话 |

### 5. 目录结构变化

```
server-12-MultiChat/
├── include/                    # ★ 新增: 头文件目录
│   ├── SessionManager.h        # ★ 核心新增
│   ├── HttpServer.h
│   └── ...
├── src/
│   ├── main.cpp
│   └── inference/
│       ├── ModelManager.h      # ★ 核心新增
│       └── ModelManager.cpp
└── UI/
    ├── multichat.html          # ★ ChatGPT风格UI
    └── multichat.css
```

---

## Server-12 → Server-13：KV Cache 性能优化

### 1. 核心差异概览

| 维度 | Server-12 | Server-13 |
|------|-----------|-----------|
| 推理方式 | 每次处理完整历史 | **增量推理（只处理新token）** |
| KV Cache | 临时创建，每次释放 | **持久化，跨轮次复用** |
| 性能 | 基准 | **5-10倍提升** |
| 会话存储 | `ChatSession` | **`KVCachedSession`** |

### 2. 核心改进：KVCachedSession

**文件位置**: `server-13-KVcache/src/inference/ModelManager.h:158-254`

```cpp
// server-13-KVcache/src/inference/ModelManager.h:198-254
struct KVCachedSession {
    // ========== 对话历史（同Server-12）==========
    ChatSession session;

    // ========== ★ KV Cache 状态（Server-13 新增）==========

    // 持久化的推理上下文（包含KV缓存）
    struct llama_context* ctx = nullptr;

    // 已处理的token数量（KV Cache中的位置）
    int n_past = 0;

    // 已缓存的token序列（用于验证缓存有效性）
    std::vector<int> cached_tokens;

    // 析构函数：自动释放context
    ~KVCachedSession();

    // 清空KV缓存（保留对话历史）
    void clearCache();
};
```

### 3. 性能提升原理

**Server-12 的问题**（每次重复计算）:
```
第1轮: tokens = [A, B, C] → 计算3个token
第2轮: tokens = [A, B, C, D, E] → 计算5个token（A,B,C重复！）
第3轮: tokens = [A, B, C, D, E, F, G] → 计算7个token（A-E重复！）
```

**Server-13 的解决方案**（增量计算）:
```
第1轮: tokens = [A, B, C] → 计算3个token，缓存KV
第2轮: tokens = [A, B, C, D, E] → 只计算[D, E]，复用[A,B,C]的KV
第3轮: tokens = [A, B, C, D, E, F, G] → 只计算[F, G]
```

### 4. ChatSession 的变化

**Server-12 版本**:
```cpp
// server-12: add() 返回 void
void add(const std::string& role, const std::string& content) {
    history.push_back({role, content});
    prune();
}
```

**Server-13 版本**:
```cpp
// server-13: add() 返回 bool，指示是否需要清空KV Cache
// server-13-KVcache/src/inference/ModelManager.h:61-64
bool add(const std::string& role, const std::string& content) {
    history.push_back({role, content});
    return prune();  // ★ 返回是否修改了历史
}
```

```cpp
// server-13: prune() 返回 bool
// server-13-KVcache/src/inference/ModelManager.h:131-155
bool prune() {
    bool modified = false;
    // 截断超长消息
    for (auto& m : history) {
        if ((int)m.content.size() > max_tokens_per_msg) {
            m.content.resize(max_tokens_per_msg);
            modified = true;  // ★ 标记修改
        }
    }
    // 删除过早的历史
    while ((int)history.size() > max_round_keep * 2) {
        history.erase(history.begin(), history.begin() + 2);
        history.insert(history.begin(), {"system", "[summary]..."});
        modified = true;  // ★ 标记修改
    }
    return modified;
}
```

### 5. ModelManager 会话字典变化

**Server-12**:
```cpp
// server-12: 只存储对话历史
std::unordered_map<std::string, ChatSession> chat_sessions_;
```

**Server-13**:
```cpp
// server-13: 存储对话历史 + KV Cache
// server-13-KVcache/src/inference/ModelManager.h:435
std::unordered_map<std::string, KVCachedSession> chat_sessions_;
```

### 6. 性能对比数据

| 测试场景 | Server-12 | Server-13 | 提升倍数 |
|----------|-----------|-----------|----------|
| 首轮对话 | 31.24s | 31.56s | 持平 |
| 第2轮对话 | 15.82s | **0.42s** | **37.6x** |
| 10轮对话总耗时 | ~100s | ~35s | **~3x** |

---

## Server-13 → Server-14：生产级认证系统

### 1. 核心差异概览

| 维度 | Server-13 | Server-14 |
|------|-----------|-----------|
| 用户认证 | 无 | **JWT认证** |
| 数据持久化 | 仅内存 | **SQLite数据库** |
| 会话隔离 | 仅session_id | **基于User ID** |
| API安全 | 公开访问 | **Bearer Token验证** |
| 模型管理 | ModelManager | **ModelManagerV2** |
| Context管理 | 简单字典 | **SessionContextPool** |

### 2. 新增核心组件

#### 2.1 JWTAuth.h - JWT认证

**文件位置**: `server-14/include/JWTAuth.h`

```cpp
// server-14/include/JWTAuth.h:19-98
class JWTAuth {
public:
    // 生成JWT Token（有效期24小时）
    static std::string generateToken(const std::string& username) {
        // 1. 创建header (固定)
        std::string header = R"({"alg":"HS256","typ":"JWT"})";

        // 2. 创建payload (用户名 + 过期时间)
        long long exp = getCurrentTimestamp() + 86400; // 24小时
        std::string payload = "{\"username\":\"" + username + "\",\"exp\":" + exp + "}";

        // 3. Base64编码
        std::string encoded_header = base64UrlEncode(header);
        std::string encoded_payload = base64UrlEncode(payload);

        // 4. HMAC-SHA256签名
        std::string data = encoded_header + "." + encoded_payload;
        std::string signature = hmacSHA256(data, secret_key);

        // 5. 组合: header.payload.signature
        return data + "." + base64UrlEncode(signature);
    }

    // 验证Token并提取用户名
    static std::string validateToken(const std::string& token) {
        // 验证签名 + 检查过期时间
        // 成功返回username，失败返回空字符串
    }

    // 设置密钥
    static void setSecretKey(const std::string& key);
};
```

#### 2.2 ModelManagerV2.h - 增强版模型管理器

**文件位置**: `server-14/include/ModelManagerV2.h`

```cpp
// server-14/include/ModelManagerV2.h:34-93
class ModelManagerV2 {
public:
    static ModelManagerV2& instance();

    // ★ 增强的加载方法
    bool loadModel(
        const std::string& path,
        int n_ctx = 2048,
        int n_threads = 4,
        int max_sessions = 100,     // ★ 新增：最大会话数
        bool enable_batch = false,  // ★ 新增：批处理开关
        int batch_size = 4          // ★ 新增：批处理大小
    );

    // ★ 核心：带KV缓存的推理
    std::string inferWithCache(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature
    );

    // ★ 新增：流式推理
    std::string inferWithCacheStreaming(
        const std::string& session_id,
        const std::string& prompt,
        int max_tokens,
        float temperature,
        StreamCallback callback  // 每个token的回调
    );

    // ★ 新增：批处理推理
    std::future<std::string> inferBatch(...);

    // ★ 新增：应用chat template
    std::string applyChatTemplate(
        const std::vector<std::pair<std::string, std::string>>& messages,
        bool add_generation_prompt = true
    ) const;

private:
    std::unique_ptr<SessionContextPool> session_pool_;  // ★ Context池
    std::unique_ptr<BatchInferenceEngine> batch_engine_; // ★ 批处理引擎
};
```

#### 2.3 SessionContextPool.h - 会话Context池

**文件位置**: `server-14/include/SessionContextPool.h`

```cpp
// server-14/include/SessionContextPool.h:27-163
class SessionContextPool {
public:
    // 会话上下文结构
    struct SessionContext {
        llama_context* ctx;              // 专属context（包含KV缓存）
        std::vector<llama_token> tokens; // 已处理的token序列
        int cached_token_count;          // 缓存的token数量
        long long last_used;             // 最后使用时间（LRU淘汰用）
    };

    SessionContextPool(llama_model* model, int max_sessions, uint32_t n_ctx, int n_threads);

    // 获取或创建会话的context
    SessionContext* getOrCreateSession(const std::string& session_id);

    // ★ 核心：增量推理
    bool processIncrementalTokens(
        const std::string& session_id,
        const std::vector<llama_token>& new_tokens,
        const std::string& prompt = ""
    );
    // 算法：
    // 1. 找到新旧token序列的公共前缀
    // 2. 只对增量部分进行llama_decode
    // 3. 更新cached_token_count

    // 池统计信息
    struct PoolStats {
        int total_sessions;        // 当前会话数
        int max_sessions;          // 最大限制
        uint64_t total_creates;    // 总创建次数
        uint64_t total_evictions;  // LRU淘汰次数
        uint64_t total_hits;       // 缓存命中
        uint64_t total_misses;     // 缓存未命中
    };
};
```

### 3. main.cpp 的变化

```cpp
// server-14/src/main.cpp:19-68
int main() {
    // ★ 新增: 初始化JWT密钥
    JWTAuth::setSecretKey("server14-secret-key-change-in-production-abc123");

    Database db("users.db");

    // ★ 使用ModelManagerV2
    const char* envModelPath = std::getenv("MODEL_PATH");
    const std::string modelPath = envModelPath ? envModelPath
        : "../models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf";

    const int n_ctx = 2048;
    const int n_threads = std::getenv("OMP_NUM_THREADS") ? std::atoi(std::getenv("OMP_NUM_THREADS")) : 4;
    const int max_sessions = 100;
    const bool enable_batch = false;
    const int batch_size = 8;

    if (!ModelManagerV2::instance().loadModel(modelPath, n_ctx, n_threads,
                                              max_sessions, enable_batch, batch_size)) {
        return 1;
    }

    // ★ SessionManager传入Database引用（持久化）
    static SessionManager sessionManager(db);

    HttpServer server(8080, 10, db);
    server.start(sessionManager);

    return 0;
}
```

### 4. 新增API接口

#### 认证接口（无需Token）

| 接口 | 方法 | 功能 |
|------|------|------|
| `/api/auth/register` | POST | 用户注册 |
| `/api/auth/login` | POST | 登录获取JWT Token |
| `/api/auth/logout` | POST | 登出 |

#### 受保护接口（需要 `Authorization: Bearer {token}`）

| 接口 | 方法 | 功能 |
|------|------|------|
| `/api/sessions/new` | POST | 创建新会话 |
| `/api/sessions` | GET | 获取**当前用户**的所有会话 |
| `/api/sessions/{id}/chat` | POST | 发送消息 |
| `/api/sessions/{id}` | DELETE | 删除会话 |
| `/api/model/status` | GET | 获取模型状态 |
| `/api/model/stats` | GET | 获取推理统计 |

### 5. 目录结构变化

```
server-14/
├── include/                          # 头文件目录（16个）
│   ├── JWTAuth.h                     # ★ 新增: JWT认证
│   ├── ModelManagerV2.h              # ★ 新增: 增强版模型管理
│   ├── SessionContextPool.h          # ★ 新增: Context池
│   ├── BatchInferenceEngine.h        # ★ 新增: 批处理引擎
│   ├── SessionManager.h              # 升级: 支持数据库持久化
│   └── ...
├── src/
│   ├── main.cpp
│   ├── ModelManagerV2.cpp            # ★ 570行实现
│   ├── SessionContextPool.cpp        # ★ 291行实现
│   └── BatchInferenceEngine.cpp      # ★ 337行实现
└── UI/
    ├── login.html                    # ★ 新增: 登录页面
    ├── register.html                 # ★ 新增: 注册页面
    └── multichat.html                # 升级: 支持认证
```

### 6. SessionManager的持久化升级

**Server-13**（内存存储）:
```cpp
SessionManager() : gen(rd()) {}  // 无参构造
```

**Server-14**（数据库持久化）:
```cpp
// 构造时传入Database引用
SessionManager(Database& db) : db_(db), gen(rd()) {
    // 从数据库加载会话
    loadSessionsFromDB();
}

void addUserMessage(const std::string& session_id, const std::string& content) {
    // ... 内存操作 ...
    // ★ 同步到数据库
    db_.saveMessage(session_id, "user", content, timestamp);
}
```

---

## 技术栈对比总结

### 各版本代码量统计

| 版本 | 核心文件数 | 核心代码行数 | 新增组件 |
|------|-----------|-------------|----------|
| Server-9 | 8 | ~2000 | - |
| Server-11 | 9 | ~3000 | SimpleInference.h (~460行) |
| Server-12 | 10 | ~3500 | SessionManager.h (~430行), ModelManager.h (~300行) |
| Server-13 | 10 | ~3800 | KVCachedSession结构体 |
| Server-14 | 16 | ~5100 | JWTAuth.h, ModelManagerV2, SessionContextPool 等 |

### 关键技术演进

| 技术领域 | Server-9 | Server-11 | Server-12 | Server-13 | Server-14 |
|----------|----------|-----------|-----------|-----------|-----------|
| HTTP服务 | epoll | epoll | epoll | epoll | epoll |
| 数据库 | SQLite | SQLite | SQLite | SQLite | SQLite + 持久化会话 |
| AI推理 | - | llama.cpp单轮 | llama.cpp多轮 | KV Cache优化 | Context池 + 批处理 |
| 会话管理 | - | - | SessionManager | KVCachedSession | SessionContextPool |
| 认证 | 简单密码 | 简单密码 | 简单密码 | 简单密码 | **JWT** |
| 性能优化 | 线程池 | 线程池 | 线程池 | **KV Cache** | **Context池 + LRU** |

### 学习路径建议

1. **Server-9 → Server-11**: 理解LLM推理基础
   - 重点阅读: `SimpleInference.h` 的 `generate()` 方法
   - 理解: Tokenization, Prefill, Decoding, KV Cache 基本概念

2. **Server-11 → Server-12**: 掌握多轮对话设计
   - 重点阅读: `SessionManager.h` 和 `ModelManager.h`
   - 理解: 会话隔离、上下文管理、ChatML格式

3. **Server-12 → Server-13**: 学习性能优化
   - 重点阅读: `KVCachedSession` 结构体
   - 理解: KV Cache持久化、增量推理原理

4. **Server-13 → Server-14**: 构建生产系统
   - 重点阅读: `JWTAuth.h`, `ModelManagerV2.h`, `SessionContextPool.h`
   - 理解: 无状态认证、对象池、LRU淘汰策略

---

## 附录：核心文件快速索引

### Server-11 核心文件
- `server-11-LLM/SimpleInference.h` - LLM推理封装
- `server-11-LLM/main.cpp` - 模型加载流程

### Server-12 核心文件
- `server-12-MultiChat/include/SessionManager.h` - 会话管理
- `server-12-MultiChat/src/inference/ModelManager.h` - 模型管理

### Server-13 核心文件
- `server-13-KVcache/src/inference/ModelManager.h` - KV Cache优化

### Server-14 核心文件
- `server-14/include/JWTAuth.h` - JWT认证 (293行)
- `server-14/include/ModelManagerV2.h` - 增强模型管理 (246行)
- `server-14/include/SessionContextPool.h` - Context池 (165行)
- `server-14/src/ModelManagerV2.cpp` - 推理实现 (570行)
- `server-14/src/SessionContextPool.cpp` - 池实现 (291行)
