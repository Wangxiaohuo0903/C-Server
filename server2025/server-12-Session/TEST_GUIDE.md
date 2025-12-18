# Server-12 测试指南

## ✅ 代码验证结果

已通过所有20项静态检查：
- ✓ 6个核心文件完整
- ✓ SessionManager类及所有方法
- ✓ 4个新API接口
- ✓ main.cpp正确集成

## 🔧 完整测试环境准备

### 前置条件

1. **Linux/WSL环境** （Server-12使用Linux系统调用）
2. **llama.cpp库**
3. **GGUF模型文件**

### 步骤1: 编译llama.cpp

```bash
# 克隆llama.cpp
git clone https://github.com/ggerganov/llama.cpp
cd llama.cpp
make

# 或使用CMake
mkdir build && cd build
cmake ..
make
```

### 步骤2: 下载模型

```bash
# 创建models目录
mkdir -p ../models

# 下载TinyLlama模型（约600MB）
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
  -O ../models/tinyllama-q4.gguf
```

### 步骤3: 编译Server-12

```bash
cd server-12-Session

g++ main.cpp -o server12 \
  -I../../llama.cpp \
  -L../../llama.cpp/build \
  -lllama \
  -lsqlite3 \
  -lpthread \
  -std=c++11
```

### 步骤4: 运行服务器

```bash
# 设置模型路径（可选）
export MODEL_PATH=../models/tinyllama-q4.gguf

# 启动服务器
./server12
```

**预期启动输出**：
```
=== Server-12: 会话管理 + 多轮对话 ===
Port: 8080

[1/4] Initializing session manager...
[SessionManager] Ready for multi-turn conversations

[2/4] Initializing inference engine...
[3/4] Loading model: ../models/tinyllama-q4.gguf
[SimpleInference] Loading model: ../models/tinyllama-q4.gguf
[SimpleInference] Model loaded successfully!
[4/4] Starting HTTP server...

💬 多轮对话接口（★ Server-12新增）:
  POST   /chat/create           - 创建新会话
  POST   /chat                  - 多轮对话
  GET    /chat/history?session_id=xxx - 获取会话历史
  DELETE /chat/delete           - 删除会话

📡 AI推理接口（继承自Server-11）:
  POST /infer-simple            - 单轮推理（无会话）

📋 用户管理接口（继承自Server-10）:
  POST /api/users/register      - JSON格式注册
  POST /api/users/login         - JSON格式登录
  GET  /api/users?id=xxx        - 获取用户信息
  POST /api/echo                - JSON回显测试

🌐 传统接口（兼容Server-9）:
  POST /register                - Form格式注册
  POST /login                   - Form格式登录
======================================
✅ Server-12 is ready!
🎯 Now supports multi-turn conversations!
======================================
```

### 步骤5: 运行测试脚本

在**另一个终端**中运行：

```bash
chmod +x test_chat.sh
./test_chat.sh
```

## 📊 预期测试结果

### Test 1: 创建会话
```bash
POST /chat/create
```
✅ **预期输出**：
```json
{"success":true,"session_id":"sess_a1b2c3d4"}
```

### Test 2: 第一轮对话
```bash
POST /chat
{"session_id":"sess_a1b2c3d4", "message":"My name is Alice."}
```
✅ **预期输出**：
```json
{"success":true,"response":"Nice to meet you, Alice!","session_id":"sess_a1b2c3d4"}
```

### Test 3: 第二轮对话（验证记忆）
```bash
POST /chat
{"session_id":"sess_a1b2c3d4", "message":"What is my name?"}
```
✅ **预期输出**：
```json
{"success":true,"response":"Your name is Alice.","session_id":"sess_a1b2c3d4"}
```
👉 **关键验证点**：模型应该记住之前说的名字是Alice！

### Test 4: 获取会话历史
```bash
GET /chat/history?session_id=sess_a1b2c3d4
```
✅ **预期输出**：
```json
{
  "success":true,
  "session_id":"sess_a1b2c3d4",
  "message_count":4,
  "history":"User: My name is Alice.\nAssistant: Nice to meet you, Alice!\nUser: What is my name?\nAssistant: Your name is Alice.\n"
}
```

### Test 5: 会话隔离验证

创建第二个会话并询问名字：
```bash
# 创建session 2
POST /chat/create
→ {"success":true,"session_id":"sess_xyz789"}

# 在session 2中询问名字
POST /chat
{"session_id":"sess_xyz789", "message":"What is my name?"}
```
✅ **预期输出**：
```json
{"success":true,"response":"I don't have information about your name.","session_id":"sess_xyz789"}
```
👉 **关键验证点**：Session 2不知道名字，证明会话隔离成功！

### Test 6: 删除会话
```bash
DELETE /chat/delete
{"session_id":"sess_a1b2c3d4"}
```
✅ **预期输出**：
```json
{"success":true,"message":"Session deleted"}
```

## 🎯 测试总结

**完整测试套件**包含11个测试用例：

1. ✅ 创建新会话
2. ✅ 第一轮对话（告诉名字）
3. ✅ 第二轮对话（询问名字，验证记忆）
4. ✅ 第三轮对话（继续对话）
5. ✅ 获取会话历史
6. ✅ 创建第二个会话
7. ✅ 在第二个会话中对话（验证会话隔离）
8. ✅ 删除会话
9. ✅ 尝试使用已删除的会话
10. ✅ 缺少参数验证
11. ✅ 单轮推理兼容性测试

## 🔍 手动测试示例

### 示例1: 完整的多轮对话流程

```bash
# 1. 创建会话
SESSION=$(curl -s -X POST http://localhost:8080/chat/create | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)
echo "Created session: $SESSION"

# 2. 告诉AI你的名字
curl -s -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"My name is Alice.\"}" | jq .

# 3. 询问AI你的名字（验证记忆）
curl -s -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"What is my name?\"}" | jq .

# 4. 查看完整历史
curl -s "http://localhost:8080/chat/history?session_id=$SESSION" | jq .
```

### 示例2: 模拟真实聊天场景

```bash
# 创建会话
SESSION=$(curl -s -X POST http://localhost:8080/chat/create | jq -r .session_id)

# 聊天轮次1
curl -s -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"I'm learning C++ programming.\"}" | jq -r .response

# 聊天轮次2
curl -s -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"What am I learning?\"}" | jq -r .response

# 应该回答：C++ programming
```

## 🐛 故障排查

### 问题1: 编译错误
```
error: llama.h: No such file or directory
```
**解决**：检查llama.cpp路径，确保-I参数正确

### 问题2: 运行时错误
```
Failed to load model!
```
**解决**：
1. 检查模型文件路径
2. 设置环境变量：`export MODEL_PATH=/path/to/model.gguf`

### 问题3: Session not found
**原因**：会话已被删除或不存在
**解决**：先调用 `/chat/create` 创建新会话

### 问题4: 上下文不连续
**检查**：
1. 确认使用同一个session_id
2. 查看会话历史确认消息已保存

## 📈 性能测试

### 并发会话测试
```bash
# 创建10个并发会话
for i in {1..10}; do
  curl -s -X POST http://localhost:8080/chat/create &
done
wait
```

### 长对话测试
```bash
# 在同一会话中进行20轮对话
SESSION=$(curl -s -X POST http://localhost:8080/chat/create | jq -r .session_id)
for i in {1..20}; do
  echo "Round $i"
  curl -s -X POST http://localhost:8080/chat \
    -H "Content-Type: application/json" \
    -d "{\"session_id\":\"$SESSION\", \"message\":\"Message $i\"}" | jq -r .response
done
```

## ✨ Server-12 vs Server-11 对比

| 功能 | Server-11 | Server-12 |
|------|-----------|-----------|
| 单轮推理 | ✅ | ✅ |
| 多轮对话 | ❌ | ✅ |
| 会话管理 | ❌ | ✅ |
| 上下文记忆 | ❌ | ✅（最近5轮）|
| 会话隔离 | N/A | ✅ |
| 历史查询 | ❌ | ✅ |

---

**编写日期**: 2025-12-15
**作者**: Claude Code
**版本**: Server-12
