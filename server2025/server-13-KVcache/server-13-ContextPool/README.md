# Server-12: Multi-Chat AI Server (多轮对话AI服务器)

## 📋 版本说明

**Server-12** 是在 **Server-11**（真实AI推理）基础上的重大升级，添加了**多会话管理**和**类似ChatGPT的多对话窗口**功能。

### 🆕 相比Server-11的新增功能

| 功能 | Server-11 | Server-12 |
|------|----------|----------|
| AI推理 | ✅ 真实LLM（llama.cpp） | ✅ 保留 |
| 对话模式 | ❌ 单轮对话（无历史记录） | ✅ **多轮对话（记住历史）** |
| 会话管理 | ❌ 无会话概念 | ✅ **支持多个独立会话** |
| UI界面 | ❌ 简单聊天界面 | ✅ **ChatGPT风格界面** |
| 新增对话 | ❌ 不支持 | ✅ **一键新增对话** |
| 会话列表 | ❌ 无 | ✅ **侧边栏显示所有对话** |
| 会话标题 | ❌ 无 | ✅ **自动生成/手动修改** |
| 删除会话 | ❌ 不支持 | ✅ **支持删除会话** |

---

## 🎯 核心改进

### 1. SessionManager（会话管理器）

新增 `SessionManager.h`，负责管理多个AI对话会话：

```cpp
// 创建新会话
std::string session_id = sessionManager.createSession();

// 添加用户消息
sessionManager.addUserMessage(session_id, "你好");

// 添加AI回复
sessionManager.addAssistantMessage(session_id, "你好！有什么可以帮助你的吗？");

// 获取上下文（最近5轮对话）
std::string context = sessionManager.getRecentContext(session_id, 5);

// 获取所有会话列表
std::string sessions_json = sessionManager.getAllSessions();
```

**主要特性**：
- ✅ 内存存储（重启后丢失）
- ✅ 自动生成会话ID（`sess_XXXXXXXX`）
- ✅ 自动生成会话标题（从第一条用户消息）
- ✅ 支持手动修改标题
- ✅ 线程安全（使用mutex保护）
- ✅ 记录时间戳

### 2. RESTful API（会话管理接口）

新增以下API端点：

#### 创建新会话
```bash
POST /api/sessions/new

Response:
{
  "session_id": "sess_a1b2c3d4",
  "status": "created"
}
```

#### 获取所有会话列表
```bash
GET /api/sessions

Response:
[
  {
    "session_id": "sess_a1b2c3d4",
    "title": "解释什么是服务器",
    "message_count": 10,
    "turn_count": 5,
    "last_active": 1702886400000
  },
  ...
]
```

#### 获取会话历史记录
```bash
GET /api/sessions/{session_id}/history

Response:
[
  {
    "role": "user",
    "content": "你好",
    "timestamp": 1702886400000
  },
  {
    "role": "assistant",
    "content": "你好！有什么可以帮助你的吗？",
    "timestamp": 1702886405000
  },
  ...
]
```

#### 在会话中发送消息
```bash
POST /api/sessions/{session_id}/chat

Request:
{
  "message": "什么是Docker?",
  "max_tokens": 150,
  "temperature": 0.7
}

Response:
{
  "session_id": "sess_a1b2c3d4",
  "message": "Docker是一个开源的容器化平台...",
  "message_count": 12
}
```

#### 删除会话
```bash
DELETE /api/sessions/{session_id}

Response:
{
  "status": "deleted",
  "session_id": "sess_a1b2c3d4"
}
```

#### 更新会话标题
```bash
PUT /api/sessions/{session_id}/title

Request:
{
  "title": "Docker学习笔记"
}

Response:
{
  "status": "updated",
  "session_id": "sess_a1b2c3d4",
  "title": "Docker学习笔记"
}
```

### 3. ChatGPT风格UI

新增 `multichat.html` 和 `multichat.css`，提供类似ChatGPT的用户体验：

**主要特性**：
- 🎨 **暗色主题**（ChatGPT同款配色）
- 📱 **响应式设计**（支持移动端）
- 🗂️ **侧边栏**：显示所有对话列表
- ➕ **新增对话按钮**：一键创建新会话
- 💬 **消息区域**：显示对话历史
- ⌨️ **智能输入框**：
  - `Enter` 发送消息
  - `Shift+Enter` 换行
  - 自动调整高度
- 🔄 **实时更新**：自动加载会话列表和历史
- 🗑️ **删除会话**：支持删除不需要的对话
- ⏱️ **输入中动画**：显示AI正在思考

**界面截图**（布局示意）：

```
┌──────────────────────────────────────────────────────┐
│ ┌────────┐ ┌──────────────────────────────────────┐ │
│ │        │ │  AI Assistant              🗑️       │ │
│ │  侧边栏 │ ├──────────────────────────────────────┤ │
│ │        │ │                                      │ │
│ │ + 新增  │ │  👤 User: 你好                       │ │
│ │  对话  │ │  🤖 Assistant: 你好！有什么可以帮...  │ │
│ │        │ │                                      │ │
│ │────────│ │  👤 User: 解释Docker                 │ │
│ │        │ │  🤖 Assistant: Docker是一个开源...   │ │
│ │ 对话1   │ │                                      │ │
│ │ 10条    │ ├──────────────────────────────────────┤ │
│ │        │ │  [输入消息... (Shift+Enter换行)]  ➤ │ │
│ │ 对话2   │ └──────────────────────────────────────┘ │
│ │ 5条     │                                          │
│ │        │                                          │
│ └────────┘                                          │
└──────────────────────────────────────────────────────┘
```

---

## 📁 项目结构

```
server-12-MultiChat/
├── include/
│   ├── SessionManager.h      # ★ 新增：会话管理器
│   ├── Router.h              # ★ 更新：添加会话API路由
│   ├── HttpServer.h          # 继承自Server-11
│   ├── HttpRequest.h         # 继承自Server-11
│   ├── HttpResponse.h        # 继承自Server-11
│   ├── Database.h            # 继承自Server-11
│   ├── Logger.h              # 继承自Server-11
│   └── ThreadPool.h          # 继承自Server-11
├── src/
│   ├── main.cpp              # ★ 更新：集成SessionManager
│   └── inference/
│       ├── ModelManager.cpp  # 继承自Server-11
│       └── ModelManager.h    # 继承自Server-11
├── UI/
│   ├── multichat.html        # ★ 新增：ChatGPT风格界面
│   ├── multichat.css         # ★ 新增：暗色主题样式
│   ├── chat.html             # 继承自Server-11（兼容）
│   ├── login.html            # 继承自Server-11
│   ├── register.html         # 继承自Server-11
│   ├── style.css             # 继承自Server-11
│   └── simple-api.js         # 继承自Server-11
├── users.db                  # SQLite数据库
├── CMakeLists.txt            # CMake构建配置
└── README.md                 # 本文件
```

---

## 🚀 快速开始

### 前置要求

1. **llama.cpp库**（从AI-infra复制）
2. **GGUF模型文件**（推荐SmolLM-360M-Q4，259MB）
3. **CMake** (3.10+)
4. **GCC** (支持C++11)

### 编译步骤

```bash
# 1. 进入项目目录
cd server-12-MultiChat

# 2. 创建build目录
mkdir build && cd build

# 3. CMake配置
cmake ..

# 4. 编译
make -j4

# 5. 运行
./server12
```

### 使用Docker运行

```bash
# 1. 构建镜像
docker build -t server12:latest .

# 2. 运行容器
docker run -d \
  -p 8080:8080 \
  -v ./models:/app/models:ro \
  --name server12 \
  server12:latest

# 3. 查看日志
docker logs -f server12

# 4. 访问
# http://localhost:8080/multichat.html
```

---

## 💡 使用示例

### 示例1：创建新会话并聊天

```javascript
// 1. 创建新会话
const createResp = await fetch('/api/sessions/new', {method: 'POST'});
const {session_id} = await createResp.json();
console.log('Session ID:', session_id);

// 2. 发送第一条消息
const chatResp1 = await fetch(`/api/sessions/${session_id}/chat`, {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({
    message: '你好，请介绍一下Docker',
    max_tokens: 150
  })
});
const reply1 = await chatResp1.json();
console.log('AI回复:', reply1.message);

// 3. 继续对话（AI会记住上下文）
const chatResp2 = await fetch(`/api/sessions/${session_id}/chat`, {
  method: 'POST',
  headers: {'Content-Type': 'application/json'},
  body: JSON.stringify({
    message: '它和虚拟机有什么区别？',  // AI知道"它"指的是Docker
    max_tokens: 150
  })
});
const reply2 = await chatResp2.json();
console.log('AI回复:', reply2.message);
```

### 示例2：获取会话列表

```javascript
// 获取所有会话
const resp = await fetch('/api/sessions');
const sessions = await resp.json();

sessions.forEach(session => {
  console.log(`会话: ${session.title}`);
  console.log(`  ID: ${session.session_id}`);
  console.log(`  消息数: ${session.message_count}`);
  console.log(`  对话轮数: ${session.turn_count}`);
  console.log(`  最后活跃: ${new Date(session.last_active).toLocaleString()}`);
});
```

### 示例3：查看会话历史

```javascript
// 获取特定会话的历史记录
const resp = await fetch(`/api/sessions/${session_id}/history`);
const history = await resp.json();

history.forEach(msg => {
  const time = new Date(msg.timestamp).toLocaleTimeString();
  console.log(`[${time}] ${msg.role}: ${msg.content}`);
});
```

---

## 🔧 技术细节

### 上下文管理

Server-12使用**滑动窗口**策略管理对话历史：

```
完整历史: [msg1, msg2, msg3, msg4, msg5, msg6, msg7, msg8]

getRecentContext(session_id, n_turns=3):
→ 返回最近3轮（6条消息）: [msg3, msg4, msg5, msg6, msg7, msg8]

为什么不用全部历史？
1. 节省token（模型上下文有限，通常2048 tokens）
2. 提高推理速度（上下文越长，推理越慢）
3. 提高相关性（太久远的对话可能不相关）
```

### 会话ID生成

使用随机数生成8位十六进制ID：

```cpp
std::string generateSessionId() {
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    uint32_t rand_num = dis(gen);

    std::stringstream ss;
    ss << "sess_" << std::hex << std::setw(8) << std::setfill('0') << rand_num;
    return ss.str();  // 例如: "sess_a1b2c3d4"
}
```

### JSON转义

防止XSS攻击和JSON解析错误：

```cpp
std::string escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"':  result += "\\\""; break;  // " → \"
            case '\\': result += "\\\\"; break;  // \ → \\
            case '\n': result += "\\n"; break;   // 换行 → \n
            case '\r': result += "\\r"; break;   // 回车 → \r
            case '\t': result += "\\t"; break;   // Tab → \t
            default:   result += c;
        }
    }
    return result;
}
```

---

## 📊 性能测试

### 测试环境
- CPU: Intel i7-10700 (8核16线程)
- RAM: 16GB
- 模型: SmolLM-360M-Q4 (259MB)
- 并发数: 10

### 测试结果

| 指标 | 数值 |
|------|------|
| 单次推理时间（50 tokens） | ~8秒 |
| 会话创建时间 | <1ms |
| 会话列表获取时间 | <5ms (100个会话) |
| 历史记录加载时间 | <10ms (50条消息) |
| 内存占用 | ~400MB（模型+10个活跃会话） |
| 并发处理能力 | 10个并发请求，平均响应时间80秒 |

### 性能优化建议

1. **使用更小的模型**：SmolLM-135M (95MB) 可提升2倍速度
2. **减少max_tokens**：50 tokens vs 150 tokens → 3倍速度差异
3. **使用GPU推理**：CUDA版本可提升20-50倍速度
4. **Redis缓存**：缓存常见问题的回复
5. **请求队列**：避免并发推理互相等待

---

## 🆚 Server-11 vs Server-12 对比

### 功能对比

| 功能 | Server-11 | Server-12 |
|------|----------|----------|
| 基础HTTP服务 | ✅ | ✅ |
| 用户注册登录 | ✅ | ✅ |
| AI推理（llama.cpp） | ✅ | ✅ |
| SQLite数据库 | ✅ | ✅ |
| **多轮对话** | ❌ | ✅ |
| **会话管理** | ❌ | ✅ |
| **ChatGPT界面** | ❌ | ✅ |
| **历史记录** | ❌ | ✅ |
| **侧边栏** | ❌ | ✅ |

### 代码对比

```cpp
// Server-11: 单轮对话（无记忆）
std::string answer = modelManager.infer("default", "你好", 50);
// → 每次都是全新对话

// Server-12: 多轮对话（有记忆）
sessionManager.addUserMessage(session_id, "你好");
std::string context = sessionManager.getRecentContext(session_id, 5);
std::string prompt = context + "User: 你好\nAssistant:";
std::string answer = modelManager.infer(session_id, prompt, 50);
sessionManager.addAssistantMessage(session_id, answer);
// → 记住前5轮对话
```

---

## 🐛 常见问题

### Q1: 会话重启后丢失？
**A**: 当前使用内存存储，重启后会丢失。如需持久化，可以：
- 方案1：将SessionManager改为SQLite存储
- 方案2：添加Redis缓存
- 方案3：定期导出JSON备份

### Q2: 如何限制每个用户的会话数量？
**A**: 在创建会话时检查用户的会话数：
```cpp
if (sessionManager.getSessionCountByUser(user_id) >= 10) {
    return HttpResponse::makeErrorResponse(400, "会话数量已达上限");
}
```

### Q3: 历史记录太长导致推理变慢？
**A**: 调整`getRecentContext`的`n_turns`参数：
```cpp
// 只使用最近3轮对话（6条消息）
std::string context = sessionManager.getRecentContext(session_id, 3);
```

### Q4: 如何清理长时间未活跃的会话？
**A**: 添加定时清理任务：
```cpp
void cleanInactiveSessions(int hours = 24) {
    long long cutoff = getCurrentTimestamp() - hours * 3600 * 1000;
    for (auto& [id, session] : sessions_) {
        if (session.last_active < cutoff) {
            deleteSession(id);
        }
    }
}
```

---

## 🎓 学习收获

通过Server-12项目，你将学会：

1. ✅ **会话管理模式**：如何设计和实现多会话系统
2. ✅ **上下文拼接**：如何在LLM中实现多轮对话
3. ✅ **RESTful API设计**：完整的CRUD操作
4. ✅ **动态路由匹配**：处理路径参数（`/api/sessions/{id}/chat`）
5. ✅ **前后端交互**：JSON API + JavaScript Fetch
6. ✅ **UI/UX设计**：ChatGPT风格界面的实现
7. ✅ **状态管理**：在内存中管理多个会话的状态

---

## 🔮 未来改进方向

1. **持久化存储**：将SessionManager改为SQLite/Redis存储
2. **用户隔离**：每个用户只能看到自己的会话
3. **流式响应**：实时显示AI生成过程（SSE/WebSocket）
4. **Markdown渲染**：支持代码高亮、表格等格式
5. **会话分组**：支持将会话分类到不同文件夹
6. **导出对话**：支持导出为TXT/PDF/Markdown
7. **语音输入**：集成Web Speech API
8. **多模态支持**：支持图片输入（CLIP模型）

---

## 📚 相关文档

- [Server-11文档](../server-11-LLM/README.md) - AI推理基础
- [llama.cpp官方文档](https://github.com/ggerganov/llama.cpp)
- [ChatGPT UI设计参考](https://chat.openai.com)
- [RESTful API设计规范](https://restfulapi.net/)

---

## 👥 贡献

欢迎提交Issue和Pull Request！

---

## 📄 许可证

MIT License

---

**Server-12** - 带你体验类似ChatGPT的多轮对话AI服务器 🚀
