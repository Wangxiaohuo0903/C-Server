# 从 Server-11 独立部署迁移到统一 Docker 部署

## 概述

如果你之前使用的是 Server-11 的独立 Dockerfile，现在可以迁移到统一 Docker 镜像，获得以下好处：

✅ **一个镜像运行多个服务器** - 不再需要多个 Dockerfile
✅ **节省空间** - 共享 llama.cpp，节省 ~600MB
✅ **灵活切换** - 无需重新构建即可切换服务器
✅ **更好的维护性** - 只需维护一个 Dockerfile

---

## 快速对比

### 旧方式（Server-11 独立）

**Dockerfile 位置**:
```
server2025/server-11-LLM/Dockerfile
```

**构建命令**:
```bash
cd server-11-LLM
docker build -t yourusername/ai-server2026:latest .
```

**镜像大小**: 669MB

**运行命令**:
```bash
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

**限制**:
- ❌ 只能运行 Server-11
- ❌ 无法切换到其他服务器
- ❌ 每个服务器需要单独构建镜像

---

### 新方式（统一镜像）

**Dockerfile 位置**:
```
server2025/Dockerfile
```

**构建命令**:
```bash
cd server2025
docker build -t yourusername/server2025:latest .
```

**镜像大小**: 933MB（包含 6 个服务器！）

**运行命令（Server-11）**:
```bash
docker run -d \
  --name server11 \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"
```

**优势**:
- ✅ 可运行 6 个服务器（7, 8, 11, 12, 13, 14）
- ✅ 交互式选择服务器
- ✅ 共享 llama.cpp 库
- ✅ 只需一个 Dockerfile

---

## 迁移步骤

### 步骤 1: 备份旧镜像（可选）

如果你想保留旧镜像作为备份：

```bash
docker tag yourusername/ai-server2026:latest \
  yourusername/ai-server2026:server11-only
```

---

### 步骤 2: 构建新镜像

```bash
# 进入 server2025 根目录
cd /Users/xiaohuo/Documents/Code/项目一代码/server2025

# 构建新镜像
docker build -t yourusername/server2025:latest .
```

---

### 步骤 3: 测试新镜像

**测试 Server-11（确保功能一致）：**

```bash
# 清理旧容器
docker stop server11-student 2>/dev/null || true
docker rm server11-student 2>/dev/null || true

# 使用新镜像运行 Server-11
docker run -d \
  --name server11-test \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"

# 等待模型加载
sleep 30

# 测试 API
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

**预期结果**: 应该与旧版本 Server-11 功能完全一致

---

### 步骤 4: 推送新镜像

```bash
# 登录 Docker Hub
docker login

# 推送新镜像
docker push yourusername/server2025:latest
```

---

### 步骤 5: 更新文档

如果你已经分发了学生使用指南，需要更新：

**旧版本**:
```bash
docker pull yourusername/ai-server2026:latest

docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

**新版本**:
```bash
docker pull yourusername/server2025:latest

docker run -d \
  --name server11 \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"
```

**主要变化**:
1. 镜像名从 `ai-server2026` 改为 `server2025`
2. 容器名从 `server11-student` 改为 `server11`
3. 添加启动脚本: `bash -c "./start-server.sh 13"`

---

### 步骤 6: 通知学生更新

**发送更新通知邮件模板：**

```
主题：Server 2025 Docker 镜像更新通知

同学们好，

我们已经升级到新版本的 Docker 镜像，新版本包含以下改进：

✅ 一个镜像可运行多个服务器（Server-7, 8, 11, 12, 13, 14）
✅ 更灵活的服务器选择机制
✅ 优化了镜像大小和构建过程

更新步骤：

1. 停止并删除旧容器：
   docker stop server11-student && docker rm server11-student

2. 拉取新镜像：
   docker pull yourusername/server2025:latest

3. 使用新命令启动 Server-11：
   docker run -d \
     --name server11 \
     -p 8080:8080 \
     -v ~/Downloads:/app/models:ro \
     -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
     yourusername/server2025:latest \
     bash -c "./start-server.sh 13"

4. 测试是否正常工作（与之前相同）

详细文档: [附上 DEPLOYMENT_GUIDE.md 链接]

如有问题，请联系助教或老师。
```

---

## 兼容性说明

### API 完全兼容

新版本的 Server-11 与旧版本 **100% API 兼容**，所有现有的测试脚本和客户端代码无需修改。

**测试端点**:
- ✅ `POST /infer-simple` - 完全兼容
- ✅ `POST /chat/send` - 完全兼容
- ✅ `POST /chat/history` - 完全兼容

### 模型挂载兼容

模型挂载方式完全相同，无需修改：

```bash
-v ~/Downloads:/app/models:ro \
-e MODEL_PATH=/app/models/your-model.gguf
```

---

## 常见问题

### Q1: 旧镜像还能用吗？

**A:** 可以，旧镜像 `yourusername/ai-server2026:latest` 仍然可以使用，但建议迁移到新版本以获得更多功能。

---

### Q2: 需要重新下载模型吗？

**A:** 不需要！模型文件可以继续使用，无需重新下载。

---

### Q3: 学生已经在使用旧版本，如何平滑过渡？

**A:** 可以采用渐进式迁移：

1. **本周**: 继续使用旧版本，发布迁移通知
2. **下周**: 提供新旧两个版本的文档
3. **第三周**: 全面切换到新版本

---

### Q4: 新镜像更大了，为什么说节省空间？

**A:**
- 旧方式: Server-11 (669MB) + Server-12 (669MB) + ... = **多个镜像**
- 新方式: 统一镜像 (933MB) = **一个镜像包含所有**

如果你需要多个服务器，新方式更节省空间。

---

### Q5: 可以同时保留两个版本吗？

**A:** 可以！两个镜像可以共存：

```bash
# 旧版本（只有 Server-11）
yourusername/ai-server2026:latest

# 新版本（统一镜像）
yourusername/server2025:latest
```

---

## 命令对照表

| 操作 | 旧命令 | 新命令 |
|------|--------|--------|
| 拉取镜像 | `docker pull yourusername/ai-server2026:latest` | `docker pull yourusername/server2025:latest` |
| 运行 Server-11 | `docker run -d --name server11-student -p 8080:8080 -v ~/Downloads:/app/models:ro -e MODEL_PATH=... yourusername/ai-server2026:latest` | `docker run -d --name server11 -p 8080:8080 -v ~/Downloads:/app/models:ro -e MODEL_PATH=... yourusername/server2025:latest bash -c "./start-server.sh 13"` |
| 查看日志 | `docker logs server11-student` | `docker logs server11` |
| 停止服务 | `docker stop server11-student` | `docker stop server11` |

---

## 回滚方案

如果遇到问题，可以随时回滚到旧版本：

```bash
# 停止新版本
docker stop server11 && docker rm server11

# 运行旧版本
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

---

## 迁移检查清单

教师端：
- [ ] 构建新镜像成功
- [ ] 测试 Server-11 功能正常
- [ ] 测试 Server-7/8 等其他服务器
- [ ] 推送新镜像到 Docker Hub
- [ ] 更新学生文档
- [ ] 准备迁移通知邮件
- [ ] 设置答疑时间

学生端：
- [ ] 拉取新镜像
- [ ] 停止旧容器
- [ ] 使用新命令启动
- [ ] 测试 API 功能
- [ ] 验证模型加载正常

---

**迁移完成后，享受统一 Docker 镜像带来的便利！🎉**
