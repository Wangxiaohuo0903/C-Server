# AI-chats Docker 部署指南

**更新日期**: 2025-12-10
**状态**: ✅ Docker 支持已添加

---

## 📋 前置要求

- **Docker**: 20.10+
- **Docker Compose**: 2.0+
- **系统**: Windows/Linux/macOS
- **模型文件**: tinyllama-q4.gguf（已包含在 ../models/）

---

## 🚀 快速开始（5 分钟）

### 1. 构建镜像

```bash
cd AI-chats
docker-compose build
```

**预计时间**: 3-5 分钟（取决于网络速度和 CPU 性能）

### 2. 启动服务

```bash
docker-compose up -d
```

### 3. 验证运行

```bash
# 检查容器状态
docker-compose ps

# 查看日志
docker-compose logs -f

# 测试 API
curl http://localhost:8080/
```

---

## 📖 详细说明

### 项目结构

```
AI-chats/
├── Dockerfile              # Docker 镜像定义
├── docker-compose.yml      # Docker Compose 配置
├── .dockerignore           # Docker 忽略文件
├── src/                    # 源代码
│   ├── main.cpp
│   └── inference/
│       ├── ModelManager.h
│       └── ModelManager.cpp
├── include/                # 头文件
│   ├── HttpServer.h        # HTTP 服务器（epoll）
│   ├── Database.h          # SQLite 数据库
│   ├── FileUtils.h         # 文件工具
│   └── ...
├── users.db                # SQLite 数据库（自动创建）
└── CMakeLists.txt          # 构建配置
```

### Dockerfile 说明

**多阶段构建**:

1. **构建阶段**（builder）:
   - 基于 Ubuntu 22.04
   - 安装编译依赖：cmake, g++, git, libsqlite3-dev
   - 克隆 llama.cpp（如果不存在）
   - 编译 AI-chats

2. **运行阶段**:
   - 基于 Ubuntu 22.04（精简）
   - 只安装运行时依赖：libsqlite3-0
   - 复制编译好的可执行文件
   - 暴露端口 8080

**镜像大小**: 约 200-300 MB（已优化）

### docker-compose.yml 说明

```yaml
services:
  ai-chats:
    build: .
    ports:
      - "8080:8080"        # HTTP 端口映射
    volumes:
      - ../models:/app/models:ro      # 模型目录（只读）
      - ./users.db:/app/users.db      # 数据库持久化
    environment:
      - MODEL_PATH=/app/models/tinyllama-q4.gguf
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "wget", "--tries=1", "http://localhost:8080/"]
      interval: 30s
      timeout: 10s
      retries: 3
```

**关键配置**:
- ✅ **端口映射**: 主机 8080 → 容器 8080
- ✅ **模型挂载**: 只读挂载（节省空间）
- ✅ **数据库持久化**: 用户数据保存到主机
- ✅ **健康检查**: 每 30 秒检查一次服务状态
- ✅ **自动重启**: 异常退出时自动重启

---

## 🔧 常用命令

### 容器管理

```bash
# 启动服务（后台运行）
docker-compose up -d

# 停止服务
docker-compose down

# 重启服务
docker-compose restart

# 查看状态
docker-compose ps

# 查看日志（实时）
docker-compose logs -f

# 查看日志（最近 100 行）
docker-compose logs --tail=100

# 进入容器（调试）
docker-compose exec ai-chats /bin/bash
```

### 镜像管理

```bash
# 重新构建镜像
docker-compose build --no-cache

# 查看镜像
docker images | grep ai-chats

# 删除镜像
docker rmi ai-chats:latest

# 清理未使用的镜像
docker image prune -a
```

---

## 🧪 功能测试

### 1. 基础连通性测试

```bash
# 测试首页
curl http://localhost:8080/

# 期望输出: Welcome to AI Infra Server!
```

### 2. 用户注册测试

```bash
curl -X POST http://localhost:8080/register \
  -H "Content-Type: application/json" \
  -d '{"username": "test", "password": "123456"}'

# 期望输出: {"message":"User registered successfully"}
```

### 3. 用户登录测试

```bash
curl -X POST http://localhost:8080/login \
  -H "Content-Type: application/json" \
  -d '{"username": "test", "password": "123456"}'

# 期望输出: {"message":"Login successful"}
```

### 4. AI 推理测试

```bash
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": "test_session_1",
    "prompt": "你好，请介绍一下自己",
    "max_tokens": 100,
    "temperature": 0.7
  }'

# 期望输出:
# {
#   "response": "你好！我是一个AI助手..."
# }
```

### 5. 多轮对话测试

```bash
# 第一轮
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": "session_001",
    "prompt": "我叫小明",
    "max_tokens": 50,
    "temperature": 0.7
  }'

# 第二轮（测试记忆）
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "chat_id": "session_001",
    "prompt": "我叫什么名字？",
    "max_tokens": 50,
    "temperature": 0.7
  }'

# 期望：AI 能记住你叫"小明"
```

---

## 🔍 故障排查

### 问题 1: 容器无法启动

**症状**:
```bash
docker-compose up -d
# ERROR: ...
```

**解决方案**:
```bash
# 1. 查看详细日志
docker-compose logs

# 2. 检查端口占用
netstat -ano | findstr :8080  # Windows
lsof -i :8080                  # Linux/Mac

# 3. 检查模型文件
ls -lh ../models/tinyllama-q4.gguf

# 4. 重新构建
docker-compose down
docker-compose build --no-cache
docker-compose up -d
```

---

### 问题 2: 模型加载失败

**症状**:
```
Model load failed
```

**解决方案**:
```bash
# 1. 检查模型路径
docker-compose exec ai-chats ls -l /app/models/

# 2. 检查模型文件权限
chmod 644 ../models/tinyllama-q4.gguf

# 3. 检查 volume 挂载
docker-compose config | grep volumes
```

---

### 问题 3: 推理响应慢

**症状**:
- 第一次推理需要 10+ 秒
- 后续推理也很慢

**原因**:
- 模型加载需要时间（首次）
- CPU 推理性能有限

**优化方案**:
```bash
# 1. 增加线程数（修改 main.cpp）
# ModelManager::instance().loadModel(modelPath, 2048, 8);  // 8 线程

# 2. 使用更小的模型
# smollm-360m-q4.gguf（259MB，更快）

# 3. 限制生成长度
# "max_tokens": 50  # 减少到 50

# 4. 查看容器资源使用
docker stats ai-chats-server
```

---

### 问题 4: 数据库错误

**症状**:
```
database is locked
```

**解决方案**:
```bash
# 1. 停止所有访问
docker-compose down

# 2. 删除数据库文件
rm users.db

# 3. 重新启动（自动创建新数据库）
docker-compose up -d
```

---

## 📊 性能优化

### 1. CPU 优化

```bash
# 限制容器 CPU 使用（docker-compose.yml）
services:
  ai-chats:
    cpus: '4.0'        # 使用 4 核
    mem_limit: 4g      # 限制内存 4GB
```

### 2. 模型选择

| 模型 | 大小 | 速度 | 质量 |
|------|------|------|------|
| **smollm-360m-q4** | 259MB | 快 | 中 |
| **tinyllama-q2** | 461MB | 中 | 中 |
| **tinyllama-q4** | 638MB | 慢 | 高 |

**推荐**: 测试用 smollm-360m-q4，生产用 tinyllama-q4

### 3. 预热模型

```bash
# 容器启动后立即发送一次推理请求（预加载模型）
docker-compose up -d && sleep 5 && \
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{"chat_id":"warmup","prompt":"hi","max_tokens":10,"temperature":0.7}'
```

---

## 🔒 安全建议

### 1. 生产环境部署

```yaml
# docker-compose.yml
services:
  ai-chats:
    environment:
      - ALLOWED_ORIGINS=https://yourdomain.com  # CORS 限制
    networks:
      - ai-network
    restart: always

networks:
  ai-network:
    driver: bridge
    internal: true  # 隔离网络
```

### 2. 反向代理（Nginx）

```nginx
server {
    listen 80;
    server_name your-domain.com;

    location / {
        proxy_pass http://localhost:8080;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }
}
```

### 3. HTTPS 支持

```bash
# 使用 Let's Encrypt
certbot --nginx -d your-domain.com
```

---

## 📈 监控和日志

### 1. 日志查看

```bash
# 实时日志
docker-compose logs -f ai-chats

# 保存日志到文件
docker-compose logs > ai-chats.log
```

### 2. 资源监控

```bash
# 查看容器资源使用
docker stats ai-chats-server

# 输出示例:
# CONTAINER        CPU %   MEM USAGE / LIMIT     MEM %
# ai-chats-server  25.5%   512MiB / 4GiB        12.5%
```

---

## 🆚 与其他版本对比

| 特性 | AI-chats (Docker) | AI-chats-linux | AI-chats-mac |
|------|-------------------|----------------|--------------|
| **多轮对话** | ✅ | ✅ | ✅ |
| **KV-Cache** | ❌ | ✅ | ✅ |
| **推测式解码** | ❌ | ✅ | ✅ |
| **部署难度** | ⭐ 简单 | ⭐⭐⭐ 中等 | ⭐⭐⭐⭐ 复杂 |
| **跨平台** | ✅ 全平台 | ❌ Linux only | ❌ macOS only |
| **性能** | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |

**推荐使用场景**:
- **学习测试**: AI-chats (Docker) ← 当前
- **Linux 服务器**: AI-chats-linux
- **macOS 开发**: AI-chats-mac

---

## 📝 下一步

1. ✅ **已完成**: Docker 化部署
2. ⏭️ **建议**: 测试所有 API 端点
3. ⏭️ **建议**: 性能基准测试
4. ⏭️ **可选**: 升级到 AI-chats-linux（KV-Cache + 推测式解码）

---

## 📞 支持

如有问题，请检查：
1. Docker 日志: `docker-compose logs`
2. 容器状态: `docker-compose ps`
3. 健康检查: `docker inspect ai-chats-server | grep Health`

---

**文档版本**: 1.0
**更新时间**: 2025-12-10
**维护者**: Claude Code
