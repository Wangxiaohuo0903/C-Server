# Server-12 快速测试指南

## 🚀 快速开始（5分钟）

### 方法1：使用现有AI-infra环境（推荐）

Server-12是基于Server-11（AI-infra/AI-chats）开发的，可以直接使用AI-infra的llama.cpp和模型文件。

#### 步骤1：确认AI-infra可用

```bash
# 检查AI-infra中的模型文件
ls ../../../AI-infra/models/
# 应该看到：smollm-360m-q4.gguf 或 tinyllama-q4.gguf

# 检查llama.cpp
ls ../../../AI-infra/third_party/llama.cpp/
# 应该看到：include/, build/, 等目录
```

#### 步骤2：创建符号链接（避免重复文件）

```bash
# 进入server-12-MultiChat目录
cd server2025/server-12-MultiChat

# 创建符号链接指向AI-infra的资源
ln -s ../../AI-infra/third_party ./third_party
ln -s ../../AI-infra/models ./models

# 验证
ls -l third_party  # 应该是指向AI-infra的链接
ls -l models       # 应该是指向AI-infra的链接
```

#### 步骤3：编译Server-12

```bash
# 创建build目录
mkdir build && cd build

# CMake配置
cmake ..

# 编译
make -j4

# 应该生成 server12 可执行文件
ls server12  # 确认存在
```

#### 步骤4：运行Server-12

```bash
# 在build目录中运行
./server12

# 应该看到类似输出：
# === Server-12: Multi-Chat AI Server ===
# ✓ Database initialized
# Loading model: ../models/smollm-360m-q4.gguf
# ✓ Model loaded successfully
# ✓ SessionManager initialized
# 🚀 Server-12 starting on port 8080...
#    Visit http://localhost:8080/multichat.html
```

#### 步骤5：访问Web界面

打开浏览器访问：
- **ChatGPT风格界面**: http://localhost:8080/multichat.html
- **旧版聊天界面**: http://localhost:8080/chat.html（Server-11兼容）

---

### 方法2：运行API测试

如果Server-12已经启动，运行测试脚本验证功能：

```bash
# Python测试（推荐）
python test_multichat.py

# 或 Bash测试（Linux/WSL）
chmod +x test_multichat.sh
./test_multichat.sh
```

**预期输出**：

```
==================================================
  Server-12 Multi-Chat API 测试
==================================================

[测试1] 创建新会话
--------------------------------------------------
响应: {
  "session_id": "sess_a1b2c3d4",
  "status": "created"
}
✓ 会话创建成功！Session ID: sess_a1b2c3d4

[测试2] 发送第一条消息
--------------------------------------------------
用户: 你好，请介绍一下什么是Docker
AI回复: Docker是一个开源的容器化平台...
✓ AI已回复

[测试3] 继续对话（多轮对话）
--------------------------------------------------
用户: 它和虚拟机有什么区别？
AI回复: 相比虚拟机，Docker更轻量...
✓ AI记住了上下文（'它'指Docker）！

... (更多测试)

==================================================
  测试完成
==================================================
✓ 多会话管理功能正常
✓ 多轮对话功能正常（AI能记住上下文）
✓ 历史记录功能正常
✓ 会话列表功能正常
✓ 更新标题功能正常
✓ 删除会话功能正常
```

---

### 方法3：手动API测试

使用curl手动测试API：

```bash
# 1. 创建新会话
curl -X POST http://localhost:8080/api/sessions/new

# 返回: {"session_id":"sess_12345678","status":"created"}

# 2. 在会话中聊天（替换YOUR_SESSION_ID）
curl -X POST http://localhost:8080/api/sessions/YOUR_SESSION_ID/chat \
  -H "Content-Type: application/json" \
  -d '{"message": "你好", "max_tokens": 50}'

# 3. 获取所有会话列表
curl http://localhost:8080/api/sessions

# 4. 获取会话历史
curl http://localhost:8080/api/sessions/YOUR_SESSION_ID/history

# 5. 删除会话
curl -X DELETE http://localhost:8080/api/sessions/YOUR_SESSION_ID
```

---

## 🎨 体验ChatGPT风格界面

### 主要功能

1. **侧边栏**：
   - 点击"+ 新增对话"创建新会话
   - 查看所有会话列表
   - 点击会话切换

2. **消息区域**：
   - 显示完整对话历史
   - 用户消息（蓝色头像👤）
   - AI回复（绿色头像🤖）

3. **输入框**：
   - 输入消息
   - `Enter` 发送
   - `Shift+Enter` 换行

4. **删除会话**：
   - 点击右上角垃圾桶图标🗑️

### 界面截图说明

```
┌──────────────────────────────────────────────────┐
│ ┌──────┐ ┌────────────────────────────────────┐ │
│ │      │ │ Docker学习笔记          🗑️      │ │
│ │ 侧边栏│ ├────────────────────────────────────┤ │
│ │      │ │                                    │ │
│ │+ 新增 │ │ 👤 你好，介绍Docker                │ │
│ │ 对话 │ │ 🤖 Docker是一个开源容器平台...     │ │
│ │      │ │                                    │ │
│ │──────│ │ 👤 它和虚拟机的区别？              │ │
│ │      │ │ 🤖 Docker更轻量，共享主机内核...   │ │
│ │Docker │ ├────────────────────────────────────┤ │
│ │学习   │ │ [输入消息...] ➤                   │ │
│ │10条   │ └────────────────────────────────────┘ │
│ │      │                                        │ │
│ │K8s   │                                        │ │
│ │入门   │                                        │ │
│ │5条    │                                        │ │
│ └──────┘                                        │ │
└──────────────────────────────────────────────────┘
```

---

## ❓ 常见问题

### Q1: 服务器启动失败？

**症状**: `Model load failed` 或 找不到模型文件

**解决方案**:
```bash
# 检查模型文件
ls models/smollm-360m-q4.gguf

# 如果不存在，创建符号链接
ln -s ../../AI-infra/models ./models

# 或设置环境变量
export MODEL_PATH=/path/to/your/model.gguf
./server12
```

### Q2: 编译错误？

**症状**: `fatal error: SessionManager.h: No such file or directory`

**解决方案**:
```bash
# 确认文件存在
ls include/SessionManager.h

# 如果不存在，从当前目录复制
# (应该已经在include目录中)
```

### Q3: API返回404？

**症状**: `curl http://localhost:8080/api/sessions` 返回 404

**原因**: Router未正确注册会话路由

**解决方案**: 检查main.cpp中是否调用了`setupSessionRoutes`

### Q4: 前端界面无法加载？

**症状**: 访问 `http://localhost:8080/multichat.html` 返回404

**解决方案**:
```bash
# 检查UI文件
ls UI/multichat.html
ls UI/multichat.css

# 检查Router.h中的setupStaticPages是否包含multichat.html
```

### Q5: AI回复很慢？

**症状**: 每次回复需要30秒以上

**解决方案**:
1. 使用更小的模型（SmolLM-135M）
2. 减少max_tokens（从150减到50）
3. 使用GPU版本的llama.cpp

---

## 📊 性能对比

| 模型 | 文件大小 | 推理速度 | 质量 | 推荐场景 |
|------|---------|---------|------|---------|
| SmolLM-360M-Q4 | 259MB | ~2-3 tok/s | ⭐⭐⭐ | ✅ 推荐（快速测试） |
| TinyLlama-1.1B-Q4 | 638MB | ~1-1.5 tok/s | ⭐⭐⭐⭐ | 需要更高质量 |
| SmolLM-135M-Q4 | 95MB | ~4-5 tok/s | ⭐⭐ | 极速测试 |

---

## 🎯 下一步

1. ✅ 测试多轮对话功能
2. ✅ 体验ChatGPT风格界面
3. ✅ 创建多个会话
4. ✅ 测试会话删除
5. ⏭️ 阅读完整README了解更多API
6. ⏭️ 根据需求定制UI样式
7. ⏭️ 集成到自己的项目中

---

**祝测试愉快！** 🚀

有问题查看 [README.md](./README.md) 或提交Issue。
