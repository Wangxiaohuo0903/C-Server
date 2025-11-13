# AI-Chats-Linux 快速参考

## 🚀 一键命令

### 启动服务
```bash
cd AI-chats-linux && docker-compose up -d
```

### 查看日志
```bash
docker-compose logs -f
```

### 停止服务
```bash
docker-compose down
```

### 测试 API
```bash
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Hello","chat_id":"test"}'
```

---

## 📋 常用命令速查

| 操作 | 命令 |
|------|------|
| 启动服务 | `docker-compose up -d` |
| 停止服务 | `docker-compose down` |
| 重启服务 | `docker-compose restart` |
| 查看状态 | `docker-compose ps` |
| 实时日志 | `docker-compose logs -f` |
| 最近日志 | `docker-compose logs --tail 100` |
| 进入容器 | `docker exec -it ai-chats-server /bin/bash` |
| 重新构建 | `docker-compose build --no-cache` |
| 查看资源 | `docker stats ai-chats-server` |
| 健康检查 | `docker inspect ai-chats-server \| grep Health` |

---

## 🔧 故障排除

### 端口被占用
```bash
# Windows
netstat -ano | findstr :8081

# Linux
netstat -tuln | grep 8081

# 解决：修改 docker-compose.yml 中的端口
ports:
  - "8082:8080"
```

### 模型加载失败
```bash
# 检查模型文件
ls -lh models/tinyllama-q4.gguf

# 重新下载
curl -L -o models/tinyllama-q4.gguf \
  "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
```

### API 超时
```bash
# 绕过代理
curl --noproxy "*" http://localhost:8081/infer ...

# 增加超时
curl --max-time 60 http://localhost:8081/infer ...
```

### 容器崩溃
```bash
# 查看错误日志
docker logs ai-chats-server --tail 50

# 检查资源限制
docker inspect ai-chats-server | grep -A 10 Memory

# 增加内存（docker-compose.yml）
deploy:
  resources:
    limits:
      memory: 12G
```

---

## 🌐 API 接口

### POST /infer - 推理
```bash
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "你的问题",
    "chat_id": "session-id",
    "max_tokens": 128,
    "temperature": 0.7
  }'
```

### POST /reset - 重置会话
```bash
curl -X POST http://localhost:8081/reset \
  -H "Content-Type: application/json" \
  -d '{"chat_id": "session-id"}'
```

### POST /register - 注册用户
```bash
curl -X POST http://localhost:8081/register \
  -H "Content-Type: application/json" \
  -d '{"username": "user", "password": "pass"}'
```

---

## 📊 性能调优

### CPU 优化
```yaml
# docker-compose.yml
deploy:
  resources:
    limits:
      cpus: '8'  # 增加 CPU 核心数

environment:
  - INFER_THREADS=8  # 匹配 CPU 核心数
```

### 内存优化
```yaml
deploy:
  resources:
    limits:
      memory: 16G  # 增加内存限制
```

### 推理速度优化
```bash
# 使用更小的模型
# 减少 max_tokens
# 降低 temperature
curl -X POST http://localhost:8081/infer \
  -d '{"prompt":"test","max_tokens":32,"temperature":0.3}'
```

---

## 📦 数据管理

### 备份数据
```bash
docker run --rm \
  -v ai-chats-linux_ai-chats-data:/data \
  -v $(pwd):/backup \
  ubuntu tar czf /backup/backup-$(date +%Y%m%d).tar.gz /data
```

### 恢复数据
```bash
docker run --rm \
  -v ai-chats-linux_ai-chats-data:/data \
  -v $(pwd):/backup \
  ubuntu tar xzf /backup/backup-20251110.tar.gz -C /
```

### 清理数据
```bash
# 停止并删除所有数据（谨慎！）
docker-compose down -v

# 只删除日志
rm -rf AI-chats-linux/logs/*
```

---

## 🔍 监控检查

### 检查容器状态
```bash
# 简单检查
docker ps | grep ai-chats

# 详细健康状态
docker inspect ai-chats-server | grep -A 20 "Health"

# 资源使用情况
docker stats ai-chats-server --no-stream
```

### 检查网络连接
```bash
# Windows
Test-NetConnection -ComputerName 127.0.0.1 -Port 8081

# Linux/Mac
nc -zv 127.0.0.1 8081

# 或使用 curl
curl -I http://localhost:8081/
```

### 检查日志中的错误
```bash
# 查找错误
docker logs ai-chats-server 2>&1 | grep -i error

# 查找警告
docker logs ai-chats-server 2>&1 | grep -i warning

# 查找关键启动信息
docker logs ai-chats-server 2>&1 | grep -E "(loaded|started|listening)"
```

---

## 🛠 开发调试

### 进入容器调试
```bash
# 进入容器
docker exec -it ai-chats-server /bin/bash

# 检查进程
ps aux | grep ai_infra

# 检查端口监听
netstat -tlnp | grep 8080

# 检查模型文件
ls -lh /app/models/

# 手动启动服务器（用于调试）
cd /app/AI-chats-linux/build
./ai_infra_server_mac
```

### 重新构建开发版本
```bash
# 修改代码后重建
docker-compose build

# 无缓存完全重建
docker-compose build --no-cache

# 强制重新创建容器
docker-compose up -d --force-recreate
```

---

## 📝 配置文件位置

| 文件 | 路径 | 说明 |
|------|------|------|
| Docker 编排 | `AI-chats-linux/docker-compose.yml` | 容器配置 |
| 镜像定义 | `AI-chats-linux/Dockerfile` | 构建配置 |
| 模型文件 | `models/tinyllama-q4.gguf` | LLM 模型 |
| 源代码 | `AI-chats-linux/src/` | C++ 源码 |
| 编译配置 | `AI-chats-linux/CMakeLists.txt` | CMake 配置 |
| 用户数据库 | Docker volume: `ai-chats-data` | SQLite 数据库 |

---

## 🚨 紧急恢复

### 服务完全无响应
```bash
# 1. 强制停止
docker-compose down -t 1

# 2. 清理容器
docker rm -f ai-chats-server

# 3. 清理网络
docker network prune -f

# 4. 重新启动
docker-compose up -d
```

### 磁盘空间不足
```bash
# 清理所有未使用的 Docker 资源
docker system prune -a --volumes

# 仅清理镜像
docker image prune -a

# 仅清理容器
docker container prune
```

### 镜像损坏
```bash
# 删除镜像
docker rmi ai-chats-linux-ai-server

# 重新构建
docker-compose build --no-cache
```

---

## 📌 环境变量参考

| 变量名 | 默认值 | 说明 |
|--------|--------|------|
| `TZ` | Asia/Shanghai | 时区设置 |
| `MODEL_PATH` | /app/models/tinyllama-q4.gguf | 模型文件路径 |
| `INFER_THREADS` | 4 | 推理线程数 |
| `INFER_CTX_SIZE` | 2048 | 上下文窗口大小 |

### 修改环境变量
```yaml
# docker-compose.yml
environment:
  - TZ=UTC
  - MODEL_PATH=/app/models/custom-model.gguf
  - INFER_THREADS=8
  - INFER_CTX_SIZE=4096
```

---

## 🔗 相关文档

- **完整部署指南**: `DEPLOYMENT.md`
- **快速开始**: `QUICKSTART.md`
- **Docker 详细说明**: `DOCKER_GUIDE.md`
- **技术迁移说明**: `MIGRATION_NOTES.md`
- **项目说明**: `README.md`

---

**提示**: 使用 Ctrl+F 快速搜索本文档中的命令
