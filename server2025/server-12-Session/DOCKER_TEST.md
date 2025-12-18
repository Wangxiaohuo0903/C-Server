# Server-12 Docker测试指南

## 🐳 快速开始

### 前置条件
- ✅ Docker Desktop已安装并运行
- ⚠️ 需要下载GGUF模型文件（约600MB）

### 方法1: 使用PowerShell脚本（推荐）

```powershell
# 在PowerShell中运行
.\test_docker.ps1
```

这个脚本会自动：
1. ✅ 检查Docker状态
2. ✅ 创建必要目录
3. ✅ 检查模型文件
4. ✅ 构建Docker镜像
5. ✅ 启动容器
6. ✅ 运行API测试

### 方法2: 手动步骤

#### 步骤1: 准备模型文件

```bash
# 创建models目录
mkdir models

# 下载TinyLlama模型
# 访问: https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF
# 下载: tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf
# 保存到: models/tinyllama-q4.gguf
```

#### 步骤2: 构建镜像

```bash
docker-compose build
```

#### 步骤3: 启动容器

```bash
docker-compose up -d
```

#### 步骤4: 查看日志

```bash
docker logs server12-chat -f
```

## 📊 预期输出

### 构建过程
```
[+] Building llama.cpp...
[+] Compiling Server-12...
[+] Creating image...
Successfully built server12-chat
```

### 启动日志
```
=== Server-12: 会话管理 + 多轮对话 ===
Port: 8080

[1/4] Initializing session manager...
[SessionManager] Ready for multi-turn conversations

[2/4] Initializing inference engine...
[3/4] Loading model: /app/models/tinyllama-q4.gguf
[SimpleInference] Loading model: /app/models/tinyllama-q4.gguf
[SimpleInference] Model loaded successfully!
[4/4] Starting HTTP server...

💬 多轮对话接口（★ Server-12新增）:
  POST   /chat/create
  POST   /chat
  GET    /chat/history?session_id=xxx
  DELETE /chat/delete

✅ Server-12 is ready!
🎯 Now supports multi-turn conversations!
```

## 🧪 测试API

### 使用PowerShell测试

```powershell
# 创建会话
$session = Invoke-RestMethod -Uri "http://localhost:8080/chat/create" -Method Post
$sessionId = $session.session_id

# 第一轮对话
$body = @{
    session_id = $sessionId
    message = "My name is Alice."
    max_tokens = "64"
} | ConvertTo-Json

Invoke-RestMethod -Uri "http://localhost:8080/chat" -Method Post -Body $body -ContentType "application/json"

# 第二轮对话（验证记忆）
$body = @{
    session_id = $sessionId
    message = "What is my name?"
    max_tokens = "64"
} | ConvertTo-Json

Invoke-RestMethod -Uri "http://localhost:8080/chat" -Method Post -Body $body -ContentType "application/json"
```

### 使用curl测试（Git Bash）

```bash
# 创建会话
SESSION=$(curl -s -X POST http://localhost:8080/chat/create | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)

# 第一轮对话
curl -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"My name is Alice.\", \"max_tokens\":\"64\"}"

# 第二轮对话
curl -X POST http://localhost:8080/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$SESSION\", \"message\":\"What is my name?\", \"max_tokens\":\"64\"}"
```

## 🔧 常用命令

```bash
# 查看运行中的容器
docker ps

# 查看实时日志
docker logs server12-chat -f

# 进入容器
docker exec -it server12-chat bash

# 重启容器
docker-compose restart

# 停止并删除容器
docker-compose down

# 重新构建并启动
docker-compose up -d --build
```

## 📁 目录结构

```
server-12-Session/
├── Dockerfile              # Docker镜像定义
├── docker-compose.yml      # Docker Compose配置
├── .dockerignore          # Docker忽略文件
├── test_docker.ps1        # PowerShell测试脚本
├── models/                # 模型文件目录（需手动创建）
│   └── tinyllama-q4.gguf # GGUF模型文件
├── data/                  # 数据持久化目录
│   └── users.db          # SQLite数据库
└── *.h, *.cpp            # 源代码
```

## 🐛 故障排查

### 问题1: Docker未运行
```
Error: Cannot connect to Docker daemon
```
**解决**: 启动Docker Desktop

### 问题2: 端口被占用
```
Error: Port 8080 already in use
```
**解决**: 修改docker-compose.yml中的端口映射
```yaml
ports:
  - "8081:8080"  # 使用8081端口
```

### 问题3: 模型文件未找到
```
[ERROR] Failed to load model!
```
**解决**: 
1. 确认models/tinyllama-q4.gguf文件存在
2. 检查docker-compose.yml中的MODEL_PATH环境变量

### 问题4: 构建很慢
**原因**: 需要下载并编译llama.cpp
**解决**: 耐心等待，首次构建约需5-10分钟

## ⚡ 性能说明

- **镜像大小**: 约1.5GB（包含Ubuntu + llama.cpp）
- **内存使用**: 约2-4GB（取决于模型大小）
- **CPU推理**: 使用多线程CPU推理
- **首次构建**: 约5-10分钟
- **后续启动**: 约10-30秒

## ✅ 验证清单

运行测试后，确认：

- [ ] Docker容器成功启动
- [ ] 服务器监听8080端口
- [ ] 可以创建新会话
- [ ] 第一轮对话成功
- [ ] 第二轮对话记住前面的内容
- [ ] 可以获取会话历史
- [ ] 不同会话之间相互隔离

---

**编写日期**: 2025-12-15
**版本**: Server-12 Docker
