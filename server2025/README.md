# Server 2025 - 统一 Docker 部署

## 🎯 特点

- **一个 Docker 镜像**：包含所有 Server 版本（Server-1 到 Server-14）
- **交互式选择**：进入容器后可以选择运行任意 Server
- **模型共享**：LLM 相关 Server (11, 12, 13, 14) 共享同一个模型文件
- **空间优化**：删除重复的 llama.cpp 目录，节省 ~600MB-1GB 空间
- **灵活切换**：无需重新构建镜像即可切换 Server 版本

## 📊 构建摘要

**镜像大小**: 703MB (优化后)
**编译成功**: 8 个服务器 (Server-7, 8, 10, 11, 12, 13, 13-KVcache, 14)
**跳过编译**: 1 个服务器 (Server-10-1: 需要 mongocxx)
**未编译**: 10 个简单服务器 (可按需添加)

---

## 📚 文档导航

- **[完整部署指南](./DEPLOYMENT_GUIDE.md)** - 教师和学生使用的详细指南 ⭐
- **[迁移指南](./MIGRATION_GUIDE.md)** - 从 Server-11 独立部署迁移到统一镜像
- **[构建总结](./BUILD_SUMMARY.md)** - 技术实现细节和构建记录
- **[快速开始](#-使用方法)** - 下方快速使用指南
- **[归档文档](./archived-docs/)** - 旧版本文档（已废弃）

---

## 📦 快速构建

```bash
cd /Users/xiaohuo/Documents/Code/项目一代码/server2025

# 构建统一镜像（替换为你的用户名）
docker build -t yourusername/server2025:latest .
```

**构建时间**: 约 3-5 分钟（包含 llama.cpp 编译）
**镜像大小**: 933MB

**详细构建和分发指南请查看 [DEPLOYMENT_GUIDE.md](./DEPLOYMENT_GUIDE.md)**

---

## 🚀 使用方法

> **完整使用指南**: 请查看 **[DEPLOYMENT_GUIDE.md](./DEPLOYMENT_GUIDE.md)**

### 方法 1: 交互式启动（推荐）

进入容器后手动选择运行哪个 Server：

```bash
# 不需要模型的服务器（Server-7, 8）
docker run -it --rm -p 8080:8080 yourusername/server2025:latest

# 需要模型的服务器（Server-11, 12, 13, 14）
docker run -it --rm \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest

# 进入容器后，运行启动脚本
./start-server.sh

# 根据提示选择 Server 版本（输入编号）
```

### 方法 2: 直接启动指定 Server

```bash
# 启动 Server-7 (Router)
docker run -d --name server7 -p 8080:8080 \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 7"

# 启动 Server-11 (LLM)
docker run -d --name server11 -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"
```

**测试服务：**

```bash
# 测试 Server-7
curl http://localhost:8080/

# 测试 Server-11
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

### 方法 3: 后台运行

```bash
# 后台运行 Server-11
docker run -d \
  --name server2025-llm \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  juanbing/server2025:latest \
  bash -c "./start-server.sh 13"

# 查看日志
docker logs -f server2025-llm
```

---

## 📋 可用的 Server 列表

| 编号 | Server | 描述 | 端口 | 需要模型 | 状态 |
|------|--------|------|------|----------|------|
| 1 | Server-1 | Hello World | 8080 | ❌ | ⚠️ 未编译 |
| 2 | Server-2 | HTTP 基础 | 8080 | ❌ | ⚠️ 未编译 |
| 3 | Server-3 | Logger 日志系统 | 8080 | ❌ | ⚠️ 未编译 |
| 4 | Server-4 | Database 数据库 | 8080 | ❌ | ⚠️ 未编译 |
| 5 | Server-5 | Epoll I/O 多路复用 | 8080 | ❌ | ⚠️ 未编译 |
| 6 | Server-6 | Thread Pool 线程池 | 8080 | ❌ | ⚠️ 未编译 |
| 7 | Server-7 | Router 路由系统 | 8080 | ❌ | ✅ 可用 |
| 8 | Server-8 | UI 用户界面 | 8080 | ❌ | ✅ 可用 |
| 9 | Server-9 | Nginx 反向代理 | 8080 | ❌ | ⚠️ 未编译 |
| 10 | Server-10 | JSON + RESTful API | 8080 | ❌ | ✅ 可用 |
| 11 | Server-10-1 | MongoDB 集成 | 8080 | ❌ | ❌ 跳过 (需要 mongocxx) |
| 12 | Server-11-1 | SSL/TLS 加密 | 8080 | ❌ | ⚠️ 未编译 |
| 13 | Server-11 | LLM 推理服务 | 8080 | ✅ | ✅ 可用 |
| 14 | Server-12 | MultiChat 多轮对话 | 8080 | ✅ | ✅ 可用 |
| 15 | Server-13 | ContextPool 上下文池 | 8080 | ✅ | ✅ 可用 |
| 16 | Server-13-Batch | Batch 批处理 | 8080 | ✅ | ⚠️ 未编译 |
| 17 | Server-13-KVcache | KV Cache 优化 (5-10x 性能) | 8080 | ✅ | ✅ 可用 🚀 |
| 18 | Server-14 | DeepSeek-R1 推理链 | 8080 | ✅ | ✅ 可用 |

---

## 🔧 常用命令

### 进入运行中的容器

```bash
# 进入容器 shell
docker exec -it server2025 bash

# 在容器内切换 Server
./start-server.sh
```

### 查看日志

```bash
docker logs -f server2025
```

### 停止容器

```bash
docker stop server2025
```

### 重启容器

```bash
docker restart server2025
```

### 查看容器状态

```bash
docker ps
docker stats server2025
```

---

## 🧪 测试示例

### 测试 Server-11 (LLM)

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{
    "prompt": "你好，请介绍一下你自己",
    "max_tokens": "128"
  }'
```

### 测试 Server-7 (Router)

```bash
curl http://localhost:8080/api/test
```

---

## 📁 目录结构

容器内目录结构：

```
/app/
├── server-1/          # Server-1 二进制和源码
├── server-2/          # Server-2 二进制和源码
├── ...
├── server-11/         # Server-11 (LLM) 二进制和源码
├── server-12/         # Server-12 二进制和源码
├── server-13/         # Server-13 二进制和源码
├── server-14/         # Server-14 二进制和源码
├── models/            # 模型文件目录（需挂载）
├── AI-infra/          # llama.cpp 库
│   └── third_party/
│       └── llama.cpp/
│           └── build/bin/  # llama.cpp 动态库
└── start-server.sh    # 启动脚本
```

---

## ⚙️ 环境变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `MODEL_PATH` | `/app/models/model.gguf` | 模型文件路径 |
| `LD_LIBRARY_PATH` | `/app/AI-infra/third_party/llama.cpp/build/bin` | llama.cpp 库路径 |

---

## 💡 使用场景

### 场景 1: 教学演示

切换不同 Server 版本讲解原理：

```bash
# 启动容器
docker run -it --rm -p 8080:8080 juanbing/server2025:latest

# 演示 Server-1
./start-server.sh 1

# Ctrl+C 停止后演示 Server-2
./start-server.sh 2
```

### 场景 2: 学生实验

学生下载模型后测试 LLM Server：

```bash
docker run -it --rm \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  juanbing/server2025:latest \
  /app/start-server.sh 13
```

### 场景 3: 开发调试

后台运行多个 Server（使用不同端口）：

```bash
# Server-11 on port 8080
docker run -d --name s11 -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/model.gguf \
  juanbing/server2025:latest bash -c "./start-server.sh 13"

# Server-12 on port 8081
docker run -d --name s12 -p 8081:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/model.gguf \
  juanbing/server2025:latest bash -c "./start-server.sh 14"
```

---

## ❓ 常见问题

### Q1: 如何切换不同的 Server？

**A:** 停止当前 Server (Ctrl+C)，然后重新运行 `./start-server.sh` 选择新的 Server。

### Q2: 模型文件找不到？

**A:** 检查：
1. 挂载路径是否正确：`-v ~/Downloads:/app/models:ro`
2. MODEL_PATH 是否正确：`-e MODEL_PATH=/app/models/your-model.gguf`
3. 文件是否存在：`docker exec -it server2025 ls -lh /app/models`

### Q3: 如何使用不同的模型？

**A:** 更改 MODEL_PATH 环境变量：

```bash
docker run -it --rm \
  -v ~/models:/app/models:ro \
  -e MODEL_PATH=/app/models/TinyLlama.gguf \
  juanbing/server2025:latest \
  /app/start-server.sh 13
```

### Q4: 端口被占用？

**A:** 使用其他端口映射：

```bash
docker run -it --rm -p 8081:8080 juanbing/server2025:latest
```

---

## 🎓 学习建议

1. **按顺序学习**：从 Server-1 开始，逐步学习到 Server-14
2. **查看源码**：进入容器后查看 `/app/server-X/` 目录的源码
3. **对比差异**：切换不同 Server 观察功能演进
4. **实验参数**：修改环境变量、端口等参数观察效果

---

**祝学习愉快！🎉**
