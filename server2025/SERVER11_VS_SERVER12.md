# Server-11 vs Server-12: 从单轮问答到多轮对话

本文档详细介绍了 Server-12 相比于 Server-11 的核心升级，解析了如何让 AI 具备"记忆力"，实现类似 ChatGPT 的连续对话功能。

## 1. 核心区别：记忆力

| 特性 | Server-11 (单轮 LLM) | Server-12 (多轮 Chat) |
| :--- | :--- | :--- |
| **交互模式** | 一问一答，问完即忘 | 连续对话，记住上下文 |
| **比喻** | 像搜索引擎，每次搜索都是独立的 | 像微信聊天，能翻看聊天记录 |
| **输入** | 仅用户当前的问题 | 历史记录 + 用户当前的问题 |
| **Prompt** | "用户: 你好" | "用户: 你好\nAI: 您好\n用户: 它可以吃吗?" |
| **核心组件** | `SimpleInference` (简单推理) | `SessionManager` (会话管理) |

---

## 2. 关键技术实现

### 2.1 会话管理 (SessionManager)
Server-12 引入了 `SessionManager` 类，它的作用就像是"聊天记录管理员"。

*   **多窗口支持**：你可以同时开启多个聊天窗口（Session）。每个窗口都有一个唯一的 `session_id`（例如 `sess_a1b2c3d4`）。
*   **隔离性**：窗口 A 的聊天记录不会跑进窗口 B 里。
*   **代码位置**：`src/SessionManager.h`

```cpp
// 伪代码示例
class SessionManager {
    // 字典：Session ID -> Session 对象
    std::unordered_map<std::string, Session> sessions_;

public:
    string createSession(); // 创建新聊天
    void addMessage(id, role, content); // 存入消息
    string getHistory(id); // 获取历史
};
```

### 2.2 上下文构造 (Context Construction)
AI 模型本身并没有"记忆"，它之所以能回答"它指的是什么"，是因为我们将之前的对话**全部打包**发给了它。

**Server-11 的 Prompt**:
```text
<|user|>
它和虚拟机有什么区别？
<|assistant|>
```
> AI 困惑："它"是谁？无法回答。

**Server-12 的 Prompt (ChatML 格式)**:
```text
<|user|>
介绍一下 Docker
<|assistant|>
Docker 是一个容器化平台...
<|user|>
它和虚拟机有什么区别？
<|assistant|>
```
> AI 明白："它"指的是上一句提到的 Docker。

### 2.3 历史记录裁剪 (Pruning)
模型能处理的文字长度是有限的（例如 2048 个 Token）。如果聊天记录太长，必须删除旧消息。

Server-12 在 `ChatSession` 中实现了智能裁剪：
1.  **轮数限制**：默认保留最近 4 轮对话。
2.  **长度限制**：如果单条消息太长，会进行截断。
3.  **摘要机制**：删除旧消息时，插入 `[summary]` 标记，提示模型"前面还有内容"。

---

## 3. 代码改动对比

### 3.1 main.cpp 改动

**Server-11**:
```cpp
// 只有模型，没有会话管理
SimpleInference inference;
inference.loadModel("model.gguf");

// 路由直接调用推理
server.post("/api/chat", [](req, res) {
    string reply = inference.infer(req.body); // 直接推理
});
```

**Server-12**:
```cpp
// 1. 初始化模型
ModelManager::instance().loadModel("model.gguf");

// 2. 初始化会话管理器 (新增!)
static SessionManager sessionManager;

// 3. 启动服务器 (传入 sessionManager)
server.start(sessionManager);
```

### 3.2 路由处理 (HttpServer.h)

**Server-12 新增了多个 API 接口**：

1.  `POST /api/sessions/new`: 创建新会话。
2.  `POST /api/sessions/{id}/chat`: 发送消息（核心逻辑）。
    *   **旧逻辑**: 直接把 `req.body` 喂给模型。
    *   **新逻辑**:
        1.  解析 `session_id`。
        2.  调用 `sessionManager.addMessage(user_msg)`。
        3.  获取完整历史 `context = sessionManager.getFullContext()`。
        4.  把 `context` 喂给模型。
        5.  拿到回复，再调用 `sessionManager.addMessage(ai_msg)`。

### 3.3 前端界面 (UI/)

Server-12 增加了一个类似 ChatGPT 的 Web 界面 (`multichat.html`)：
*   左侧：会话列表（Session List）。
*   右侧：聊天气泡窗口。
*   功能：支持新建对话、切换对话、自动滚动到底部。

---

## 4. 总结

Server-12 是从"玩具"到"产品"的关键一步。它通过引入状态管理（State Management），让 AI 从一个单纯的文本生成器，变成了一个可以进行连贯交流的智能助手。

虽然 Server-12 实现了功能，但在性能上存在**重复计算历史**的问题，这也正是 Server-13 引入 KV Cache 想要解决的痛点。
