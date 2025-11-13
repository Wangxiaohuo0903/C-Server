# AI-Chats-Linux Docker 部署手册

本文档提供完整的 Docker 部署指南，适用于在任何支持 Docker 的环境中运行 AI 推理服务。

---

## 📋 目录

- [系统要求](#系统要求)
- [快速开始](#快速开始)
- [详细部署步骤](#详细部署步骤)
- [配置说明](#配置说明)
- [常用操作](#常用操作)
- [API 使用指南](#api-使用指南)
- [故障排除](#故障排除)
- [性能优化](#性能优化)

---

## 系统要求

### 硬件要求
- **CPU**: 4核心或以上（推荐8核心）
- **内存**: 8GB 或以上（推荐16GB）
- **磁盘**: 20GB 可用空间
- **网络**: 稳定的互联网连接（首次构建时下载依赖）

### 软件要求
- **Docker**: 20.10+ 或更高版本
- **Docker Compose**: 2.0+ 或更高版本
- **操作系统**:
  - Windows 10/11 (需要 WSL2)
  - Linux (Ubuntu 20.04+, CentOS 8+)
  - macOS 11+

### 验证环境
```bash
# 检查 Docker 版本
docker --version
# 输出示例: Docker version 24.0.6

# 检查 Docker Compose 版本
docker-compose --version
# 输出示例: Docker Compose version v2.20.2

# 检查 Docker 是否正常运行
docker ps
```

---

## 快速开始

### 1. 准备项目文件

```bash
# 克隆或复制项目到目标机器
cd /path/to/AI-infra

# 确认目录结构
ls -la AI-chats-linux/
# 应包含: Dockerfile, docker-compose.yml, src/, include/ 等
```

### 2. 下载模型文件

```bash
# 创建 models 目录
mkdir -p models

# 下载 TinyLlama-1.1B 模型（约 638MB）
# 方法 1: 使用 curl（推荐）
curl -L -o models/tinyllama-q4.gguf \
  "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

# 方法 2: 使用 wget
wget -O models/tinyllama-q4.gguf \
  "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

# 验证模型文件
ls -lh models/tinyllama-q4.gguf
# 应显示约 638MB
```

### 3. 一键启动

```bash
cd AI-chats-linux

# 启动服务（首次会自动构建镜像）
docker-compose up -d

# 查看启动日志
docker-compose logs -f

# 等待看到 "Cache Warmup Complete" 表示启动成功
```

### 4. 测试服务

```bash
# 测试健康检查端点
curl http://localhost:8081/

# 测试推理 API
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Hello, how are you?","chat_id":"test"}'
```

---

## 详细部署步骤

### 步骤 1: 环境准备

#### Windows 环境
```powershell
# 确保 Docker Desktop 已安装并启动
# 设置 WSL2 后端（Settings → General → Use WSL 2）

# 分配足够资源（Settings → Resources）
# - CPU: 至少 4 核心
# - Memory: 至少 8GB
# - Disk: 至少 20GB
```

#### Linux 环境
```bash
# 安装 Docker（Ubuntu 示例）
sudo apt-get update
sudo apt-get install -y docker.io docker-compose

# 启动 Docker 服务
sudo systemctl start docker
sudo systemctl enable docker

# 添加当前用户到 docker 组（避免使用 sudo）
sudo usermod -aG docker $USER
newgrp docker
```

### 步骤 2: 文件准备

```bash
# 项目目录结构
AI-infra/
├── AI-chats-linux/
│   ├── Dockerfile              # Docker 镜像定义
│   ├── docker-compose.yml      # 容器编排配置
│   ├── CMakeLists.txt         # 构建配置
│   ├── include/               # 头文件
│   │   ├── HttpServer.h       # HTTP 服务器（已改为 epoll）
│   │   └── ...
│   ├── src/                   # 源代码
│   │   ├── main.cpp
│   │   ├── inference/
│   │   │   └── ModelManager.cpp  # 模型管理（已更新 API）
│   │   └── ...
│   └── warmup_templates_code.json  # 预热模板（可选）
├── models/
│   └── tinyllama-q4.gguf      # LLM 模型文件
└── third_party/
    └── llama.cpp/             # 由 Docker 自动克隆
```

### 步骤 3: 配置检查

#### 检查 docker-compose.yml 配置
```yaml
# AI-chats-linux/docker-compose.yml
services:
  ai-server:
    # 端口映射（根据实际情况修改）
    ports:
      - "8081:8080"  # 宿主机端口:容器端口

    # 模型路径（确保与实际路径匹配）
    volumes:
      - ../models:/app/models:ro

    # 资源限制（根据机器配置调整）
    deploy:
      resources:
        limits:
          cpus: '4'
          memory: 8G
```

#### 检查模型路径
```bash
# 确认模型文件存在
ls -lh models/tinyllama-q4.gguf

# 如果使用其他模型，修改 docker-compose.yml
# environment:
#   - MODEL_PATH=/app/models/your-model.gguf
```

### 步骤 4: 构建镜像

```bash
cd AI-chats-linux

# 方法 1: 使用 docker-compose 构建（推荐）
docker-compose build

# 方法 2: 手动构建 Docker 镜像
docker build -f Dockerfile -t ai-chats-linux:latest ..

# 构建过程说明：
# 1. 下载 Ubuntu 22.04 基础镜像
# 2. 安装编译依赖（cmake, g++, libsqlite3-dev 等）
# 3. 克隆 llama.cpp（约 100MB）
# 4. 编译 C++ 代码（约 1-2 分钟）
# 5. 生成最终镜像（约 2GB）
```

### 步骤 5: 启动容器

```bash
# 启动服务（后台运行）
docker-compose up -d

# 查看容器状态
docker-compose ps
# 输出示例：
# NAME              IMAGE                     STATUS           PORTS
# ai-chats-server   ai-chats-linux-ai-server  Up 2 minutes     0.0.0.0:8081->8080/tcp

# 查看启动日志（实时）
docker-compose logs -f ai-server

# 关键日志标志：
# ✅ "[Model] loaded ok" - 模型加载成功
# ✅ "Cache Warmup Complete" - 预热完成
# ✅ 容器状态变为 "healthy" - 服务就绪
```

### 步骤 6: 验证部署

```bash
# 1. 检查容器健康状态
docker inspect ai-chats-server | grep -A 5 "Health"
# 应显示 "Status": "healthy"

# 2. 测试基础连通性
curl http://localhost:8081/
# 应返回 302 重定向或登录页面

# 3. 测试推理功能
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "什么是人工智能？",
    "chat_id": "test",
    "max_tokens": 100,
    "temperature": 0.7
  }'
# 应返回 JSON 格式的推理结果

# 4. 检查容器资源使用
docker stats ai-chats-server
```

---

## 配置说明

### docker-compose.yml 配置项详解

```yaml
services:
  ai-server:
    # ==================== 端口配置 ====================
    ports:
      - "8081:8080"
      # 格式: "宿主机端口:容器端口"
      # 如果 8081 被占用，可改为 8082、8083 等

    # ==================== 数据卷挂载 ====================
    volumes:
      # 模型文件（只读）
      - ../models:/app/models:ro
      # 用户数据库（读写，持久化）
      - ai-chats-data:/data
      # 日志目录（可选）
      - ./logs:/app/logs

    # ==================== 环境变量 ====================
    environment:
      # 时区设置
      - TZ=Asia/Shanghai
      # 模型路径（容器内路径）
      - MODEL_PATH=/app/models/tinyllama-q4.gguf
      # 推理线程数（建议设为 CPU 核心数）
      - INFER_THREADS=4
      # 上下文窗口大小
      - INFER_CTX_SIZE=2048

    # ==================== 资源限制 ====================
    deploy:
      resources:
        limits:
          cpus: '4'      # 最多使用 4 个 CPU 核心
          memory: 8G     # 最大内存限制 8GB
        reservations:
          cpus: '2'      # 保留至少 2 个核心
          memory: 4G     # 保留至少 4GB 内存

    # ==================== 重启策略 ====================
    restart: unless-stopped
    # 选项:
    # - no: 不自动重启
    # - always: 总是重启
    # - on-failure: 仅失败时重启
    # - unless-stopped: 除非手动停止，否则总是重启

    # ==================== 健康检查 ====================
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/"]
      interval: 30s      # 每 30 秒检查一次
      timeout: 5s        # 超时时间 5 秒
      retries: 3         # 连续失败 3 次标记为 unhealthy
      start_period: 30s  # 启动后 30 秒开始检查
```

### Dockerfile 关键配置

```dockerfile
# 基础镜像
FROM ubuntu:22.04

# 暴露端口（容器内端口）
EXPOSE 8080

# 启动命令
CMD ["./ai_infra_server_mac"]

# 注意事项：
# 1. llama.cpp 由 Dockerfile 自动克隆，无需手动初始化
# 2. 编译过程使用所有可用 CPU 核心（make -j$(nproc)）
# 3. 健康检查依赖 curl，已在镜像中安装
```

---

## 常用操作

### 启动和停止

```bash
# 启动服务
docker-compose up -d

# 停止服务
docker-compose down

# 重启服务
docker-compose restart

# 停止并删除所有数据（谨慎使用！）
docker-compose down -v
```

### 查看日志

```bash
# 实时查看日志
docker-compose logs -f

# 查看最近 100 行日志
docker-compose logs --tail 100

# 查看特定服务日志
docker-compose logs ai-server

# 导出日志到文件
docker-compose logs > logs.txt
```

### 进入容器调试

```bash
# 进入容器 shell
docker-compose exec ai-server /bin/bash

# 在容器内操作
cd /app/AI-chats-linux/build
./ai_infra_server_mac  # 手动启动服务器
ls /app/models/        # 检查模型文件
ps aux | grep ai_infra # 查看进程
```

### 更新和重建

```bash
# 拉取最新代码后重建镜像
docker-compose build --no-cache

# 重新启动容器
docker-compose up -d --force-recreate

# 清理旧镜像（释放空间）
docker system prune -a
```

### 数据备份和恢复

```bash
# 备份用户数据库
docker run --rm \
  -v ai-chats-linux_ai-chats-data:/data \
  -v $(pwd):/backup \
  ubuntu tar czf /backup/ai-chats-backup.tar.gz /data

# 恢复数据库
docker run --rm \
  -v ai-chats-linux_ai-chats-data:/data \
  -v $(pwd):/backup \
  ubuntu tar xzf /backup/ai-chats-backup.tar.gz -C /
```

---

## API 使用指南

### 1. 推理接口 (POST /infer)

#### 基础用法
```bash
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "写一个Python函数计算斐波那契数列",
    "chat_id": "session-123"
  }'
```

#### 完整参数
```bash
curl -X POST http://localhost:8081/infer \
  -H "Content-Type: application/json" \
  -d '{
    "user_message": "解释什么是机器学习",
    "chat_id": "unique-session-id",
    "max_tokens": 256,
    "temperature": 0.8
  }'
```

#### 参数说明
| 参数 | 类型 | 必填 | 默认值 | 说明 |
|------|------|------|--------|------|
| `user_message` | string | ✅ | - | 用户输入的提示词 |
| `chat_id` | string | ❌ | "default" | 会话 ID（用于区分不同对话） |
| `max_tokens` | integer | ❌ | 64 | 最大生成 token 数量（1-2048） |
| `temperature` | float | ❌ | 0.7 | 温度参数（0.1-2.0，越高越随机） |

#### 响应格式
```json
{
  "answer": "机器学习是人工智能的一个分支，通过算法让计算机从数据中学习规律..."
}
```

### 2. 重置会话 (POST /reset)

```bash
# 清除指定会话的 KV cache
curl -X POST http://localhost:8081/reset \
  -H "Content-Type: application/json" \
  -d '{"chat_id": "session-123"}'
```

### 3. 用户注册 (POST /register)

```bash
curl -X POST http://localhost:8081/register \
  -H "Content-Type: application/json" \
  -d '{
    "username": "testuser",
    "password": "password123"
  }'
```

### 4. 用户登录 (POST /login)

```bash
curl -X POST http://localhost:8081/login \
  -H "Content-Type: application/json" \
  -d '{
    "username": "testuser",
    "password": "password123"
  }'
```

### Python 示例

```python
import requests
import json

# 配置
API_URL = "http://localhost:8081"
CHAT_ID = "my-session"

def infer(message, max_tokens=128, temperature=0.7):
    """调用推理 API"""
    response = requests.post(
        f"{API_URL}/infer",
        json={
            "user_message": message,
            "chat_id": CHAT_ID,
            "max_tokens": max_tokens,
            "temperature": temperature
        },
        timeout=30
    )
    return response.json()["answer"]

# 使用示例
answer = infer("什么是深度学习？")
print(answer)

# 多轮对话
for question in ["什么是神经网络？", "它有什么应用？"]:
    answer = infer(question)
    print(f"Q: {question}")
    print(f"A: {answer}\n")
```

### JavaScript/Node.js 示例

```javascript
const axios = require('axios');

const API_URL = 'http://localhost:8081';
const CHAT_ID = 'my-session';

async function infer(message, maxTokens = 128, temperature = 0.7) {
    try {
        const response = await axios.post(`${API_URL}/infer`, {
            user_message: message,
            chat_id: CHAT_ID,
            max_tokens: maxTokens,
            temperature: temperature
        }, {
            timeout: 30000
        });
        return response.data.answer;
    } catch (error) {
        console.error('推理失败:', error.message);
        throw error;
    }
}

// 使用示例
(async () => {
    const answer = await infer('什么是人工智能？');
    console.log(answer);
})();
```

---

## 故障排除

### 问题 1: 容器无法启动

**症状**:
```bash
docker-compose up -d
# 输出: Error response from daemon: ...
```

**解决方案**:
```bash
# 1. 检查 Docker 服务是否运行
sudo systemctl status docker

# 2. 检查端口是否被占用
netstat -tuln | grep 8081
# 或 Windows:
netstat -ano | findstr :8081

# 3. 修改 docker-compose.yml 中的端口
ports:
  - "8082:8080"  # 改为其他端口

# 4. 查看详细错误日志
docker-compose logs
```

### 问题 2: 模型加载失败

**症状**:
```
[ERROR] Failed to load model
```

**解决方案**:
```bash
# 1. 检查模型文件是否存在
ls -lh models/tinyllama-q4.gguf

# 2. 检查模型文件完整性（应为约 638MB）
du -h models/tinyllama-q4.gguf

# 3. 重新下载模型
rm models/tinyllama-q4.gguf
curl -L -o models/tinyllama-q4.gguf \
  "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

# 4. 检查容器内路径
docker exec ai-chats-server ls -lh /app/models/
```

### 问题 3: API 请求超时

**症状**:
```bash
curl: (28) Operation timed out
```

**解决方案**:
```bash
# 1. 检查容器是否健康
docker ps | grep ai-chats
# 应显示 "healthy"

# 2. 检查防火墙/代理设置
curl --noproxy "*" http://localhost:8081/

# 3. 增加超时时间
curl --max-time 60 http://localhost:8081/infer ...

# 4. 检查容器日志
docker logs ai-chats-server --tail 50
```

### 问题 4: 内存不足 (OOM)

**症状**:
```
Error: Container killed (OOM)
```

**解决方案**:
```bash
# 1. 增加 Docker 内存限制（Docker Desktop）
# Settings → Resources → Memory: 设置为 8GB+

# 2. 修改 docker-compose.yml
deploy:
  resources:
    limits:
      memory: 12G  # 增加到 12GB

# 3. 使用更小的模型
# 下载 TinyLlama-1.1B 而非 Llama-7B

# 4. 降低上下文窗口
environment:
  - INFER_CTX_SIZE=1024  # 从 2048 降到 1024
```

### 问题 5: 推理速度慢

**症状**: 推理耗时 >10 秒

**解决方案**:
```bash
# 1. 增加 CPU 核心数
deploy:
  resources:
    limits:
      cpus: '8'  # 增加到 8 核心

# 2. 增加推理线程
environment:
  - INFER_THREADS=8  # 设置为 CPU 核心数

# 3. 减少生成 token 数量
{
  "max_tokens": 32  # 从 64 降到 32
}

# 4. 监控资源使用
docker stats ai-chats-server
```

### 问题 6: 编译失败

**症状**:
```
ERROR: failed to build
```

**解决方案**:
```bash
# 1. 清理 Docker 缓存
docker system prune -a

# 2. 检查网络连接（需要下载 llama.cpp）
curl -I https://github.com/ggerganov/llama.cpp.git

# 3. 无缓存重新构建
docker-compose build --no-cache

# 4. 检查 CMake 版本（容器内）
docker run --rm ubuntu:22.04 apt-cache policy cmake
# 应为 3.22+
```

---

## 性能优化

### 1. 硬件优化

```yaml
# docker-compose.yml 资源配置
deploy:
  resources:
    limits:
      cpus: '8'          # 使用更多 CPU 核心
      memory: 16G        # 增加内存
    reservations:
      cpus: '4'
      memory: 8G
```

### 2. 模型选择

| 模型 | 大小 | 内存需求 | 推理速度 | 质量 |
|------|------|---------|---------|------|
| TinyLlama-1.1B-Q4 | 638MB | 2GB | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| Phi-2-2.7B-Q4 | 1.6GB | 4GB | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| Llama-2-7B-Q4 | 4GB | 8GB | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

### 3. 推理参数优化

```bash
# 平衡速度和质量
{
  "max_tokens": 128,      # 适中的生成长度
  "temperature": 0.7      # 平衡创造性和确定性
}

# 追求速度
{
  "max_tokens": 32,       # 减少生成长度
  "temperature": 0.3      # 更确定性，减少采样时间
}

# 追求质量
{
  "max_tokens": 512,      # 增加生成长度
  "temperature": 0.9      # 增加创造性
}
```

### 4. 并发处理

```yaml
# 增加 HTTP 服务器线程池大小
# 修改 src/main.cpp:
ThreadPool pool(/*threads=*/8, /*queue_size=*/32);
# 然后重新构建镜像
```

### 5. 日志优化

```yaml
# 减少日志输出（生产环境）
logging:
  driver: "json-file"
  options:
    max-size: "5m"    # 减小单个日志文件大小
    max-file: "2"     # 减少日志文件数量
```

---

## 生产环境建议

### 1. 使用环境变量管理配置

```bash
# 创建 .env 文件
cat > .env <<EOF
API_PORT=8081
MODEL_PATH=/app/models/tinyllama-q4.gguf
INFER_THREADS=8
INFER_CTX_SIZE=2048
EOF

# 在 docker-compose.yml 中引用
env_file:
  - .env
```

### 2. 添加反向代理（Nginx）

```nginx
# nginx.conf
server {
    listen 80;
    server_name api.example.com;

    location /api/ {
        proxy_pass http://localhost:8081/;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 60s;
    }
}
```

### 3. 监控和告警

```bash
# 使用 Prometheus 监控
# docker-compose.yml 添加:
services:
  prometheus:
    image: prom/prometheus
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml
    ports:
      - "9090:9090"
```

### 4. 自动化部署

```bash
# deploy.sh
#!/bin/bash
set -e

echo "拉取最新代码..."
git pull

echo "构建镜像..."
docker-compose build

echo "重启服务..."
docker-compose up -d

echo "等待服务就绪..."
sleep 10

echo "健康检查..."
curl -f http://localhost:8081/ || exit 1

echo "部署成功！"
```

---

## 常见问题 (FAQ)

**Q: 如何更换模型？**

A:
```bash
# 1. 下载新模型到 models/ 目录
# 2. 修改 docker-compose.yml:
environment:
  - MODEL_PATH=/app/models/new-model.gguf
# 3. 重启容器
docker-compose restart
```

**Q: 如何增加推理速度？**

A:
1. 增加 CPU 核心和内存
2. 使用更小的模型
3. 减少 `max_tokens` 参数
4. 增加 `INFER_THREADS` 环境变量

**Q: 数据保存在哪里？**

A: 用户数据库保存在 Docker volume `ai-chats-data` 中，可通过备份命令导出。

**Q: 如何在多台机器上部署？**

A:
1. 复制整个项目目录到目标机器
2. 确保 `models/` 目录包含模型文件
3. 运行 `docker-compose up -d`

---

## 联系和支持

- **项目文档**: `README.md`, `QUICKSTART.md`
- **技术细节**: `MIGRATION_NOTES.md`
- **Docker 指南**: `DOCKER_GUIDE.md`

---

**版本**: v1.0
**最后更新**: 2025-11-10
**维护者**: AI-Chats Team
