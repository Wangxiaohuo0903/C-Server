# Docker 部署指南（Windows）

本指南帮助你在 Windows 上通过 Docker 运行 AI-chats-linux 项目。

## 前置要求

### 1. 安装 Docker Desktop for Windows

下载并安装 Docker Desktop：
- 官网：https://www.docker.com/products/docker-desktop/
- 系统要求：Windows 10/11 Pro 或 Enterprise（需要 WSL 2）

安装后验证：
```bash
docker --version
docker-compose --version
```

### 2. 准备模型文件

下载 GGUF 格式的量化模型（推荐 TinyLlama 用于测试）：

```bash
# 创建 models 目录
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra
mkdir models

# 下载 TinyLlama-1.1B-Q4 模型（约 700MB）
# 方法 1：使用 curl（Windows PowerShell）
curl -L -o models\tinyllama-q4.gguf "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

# 方法 2：手动下载
# 访问 https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF
# 下载 tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf 到 models 目录
```

### 3. 重要：代码适配 Linux

**注意**：AI-chats-linux 目前使用的是 `kqueue`（Mac/BSD 特性），需要改为 `epoll` 才能在 Linux Docker 容器中运行。

#### 修改方法：

编辑 `AI-chats-linux/include/HttpServer.h`，将：

```cpp
#include <sys/event.h>     // kqueue、kevent (Mac/BSD 的 I/O 多路复用)
```

改为：

```cpp
#include <sys/epoll.h>     // epoll (Linux 的 I/O 多路复用)
```

然后在代码中将所有 `kqueue` 相关调用改为 `epoll`：

**kqueue → epoll 映射表：**
| kqueue | epoll |
|--------|-------|
| `kqueue()` | `epoll_create1()` |
| `kevent()` | `epoll_wait()` |
| `EV_SET()` | `epoll_ctl()` |
| `struct kevent` | `struct epoll_event` |

或者使用条件编译：

```cpp
#ifdef __linux__
    #include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
    #include <sys/event.h>
#endif
```

**快速方案**：如果不想修改代码，可以先使用项目根目录的其他 Linux 兼容版本。

---

## 快速启动（3 步）

### 第 1 步：进入项目目录

```bash
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\AI-chats-linux
```

### 第 2 步：启动服务

```bash
# 使用 Docker Compose（推荐）
docker-compose up -d

# 或手动构建和运行
docker build -f Dockerfile -t ai-chats-linux:latest ..
docker run -d \
  -p 8080:8080 \
  -v C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\models:/app/models:ro \
  -v ai-chats-data:/data \
  --name ai-chats \
  ai-chats-linux:latest
```

### 第 3 步：验证服务

```bash
# 健康检查
curl http://localhost:8080/

# 测试推理 API
curl -X POST http://localhost:8080/infer ^
  -H "Content-Type: application/json" ^
  -d "{\"prompt\":\"Hello, what is 2+2?\",\"chat_id\":\"test\"}"
```

**预期响应：**
```json
{
  "answer": "2 + 2 equals 4."
}
```

---

## 详细操作指南

### 查看日志

```bash
# 实时查看日志
docker-compose logs -f ai-server

# 查看最近 100 行日志
docker logs --tail 100 ai-chats
```

### 停止和启动

```bash
# 停止服务
docker-compose down

# 启动服务
docker-compose up -d

# 重启服务
docker-compose restart
```

### 进入容器调试

```bash
# 进入容器 shell
docker-compose exec ai-server /bin/bash

# 或使用容器名
docker exec -it ai-chats /bin/bash

# 在容器内部操作
cd /app/AI-chats-linux/build
./ai_infra_server_mac  # 手动启动服务器
```

### 重新构建镜像

```bash
# 无缓存重新构建
docker-compose build --no-cache

# 或手动构建
docker build --no-cache -f Dockerfile -t ai-chats-linux:latest ..
```

### 查看资源使用

```bash
# 查看容器资源使用情况
docker stats ai-chats

# 查看容器详细信息
docker inspect ai-chats
```

---

## 常见问题

### Q1: 构建失败 - "CMake not found"

**原因**：Dockerfile 中的依赖安装失败。

**解决**：
```bash
# 检查 Dockerfile 第 13 行是否包含 cmake
# 或手动进入容器安装
docker exec -it ai-chats apt-get update && apt-get install -y cmake
```

### Q2: 运行失败 - "kqueue not found"

**原因**：代码使用了 Mac/BSD 特有的 kqueue，Linux 不支持。

**解决**：参考上方 "代码适配 Linux" 章节修改代码。

### Q3: 模型加载失败 - "Model file not found"

**原因**：模型路径不正确或模型文件不存在。

**解决**：
```bash
# 检查 models 目录
ls models/

# 检查容器内路径
docker exec ai-chats ls /app/models/

# 修改 docker-compose.yml 中的 MODEL_PATH 环境变量
```

### Q4: 端口冲突 - "Port 8080 already in use"

**原因**：端口 8080 被其他程序占用。

**解决**：
```bash
# 查找占用进程（Windows PowerShell）
netstat -ano | findstr :8080

# 修改 docker-compose.yml 映射其他端口
ports:
  - "8888:8080"  # 使用 8888 端口

# 访问时改为 http://localhost:8888/
```

### Q5: 内存不足 - "Out of memory"

**原因**：Docker Desktop 默认内存限制太小。

**解决**：
1. 打开 Docker Desktop
2. Settings → Resources → Advanced
3. 调整 Memory 至少 8GB
4. 点击 Apply & Restart

### Q6: 推理速度很慢

**优化建议**：

1. **使用更小的模型**：
   - TinyLlama-1.1B（推荐）
   - Phi-2-2.7B

2. **调整 CPU 核心数**：
   ```yaml
   # docker-compose.yml
   deploy:
     resources:
       limits:
         cpus: '8'  # 增加到 8 核
   ```

3. **减少生成长度**：
   ```bash
   curl -X POST http://localhost:8080/infer \
     -H "Content-Type: application/json" \
     -d '{"prompt":"Hello","chat_id":"test","max_tokens":32}'
   ```

### Q7: Windows 路径问题

**问题**：Windows 路径在 Docker 中不识别。

**解决**：
```bash
# 错误：C:\Users\...
# 正确：/c/Users/... 或使用相对路径

# 在 docker-compose.yml 中使用相对路径
volumes:
  - ../models:/app/models:ro  # 推荐
```

---

## 性能调优

### 1. 优化 Docker Desktop 设置

- **CPU**：分配至少 4 个核心
- **内存**：分配至少 8GB
- **磁盘**：预留 20GB 以上空间
- **WSL 2**：确保启用（Settings → General → Use WSL 2）

### 2. 优化推理参数

编辑 `docker-compose.yml`：

```yaml
environment:
  - INFER_THREADS=8        # 增加推理线程数
  - INFER_CTX_SIZE=4096    # 增加上下文长度
```

### 3. 持久化数据

数据会自动保存到 Docker volume `ai-chats-data`：

```bash
# 查看 volume
docker volume ls

# 备份数据
docker run --rm -v ai-chats-data:/data -v C:\backup:/backup ubuntu tar czf /backup/ai-chats-backup.tar.gz /data

# 恢复数据
docker run --rm -v ai-chats-data:/data -v C:\backup:/backup ubuntu tar xzf /backup/ai-chats-backup.tar.gz -C /
```

---

## 推荐的模型

| 模型 | 大小 | 内存需求 | 速度 | 适用场景 |
|------|------|---------|------|---------|
| TinyLlama-1.1B-Q4 | ~700MB | 2GB | 快 | 测试、学习 |
| Phi-2-2.7B-Q4 | ~1.6GB | 4GB | 中 | 通用对话 |
| Llama-2-7B-Q4 | ~4GB | 8GB | 慢 | 高质量输出 |
| Mistral-7B-Q4 | ~4GB | 8GB | 慢 | 代码生成 |

---

## 项目结构（容器内）

```
/app/
├── AI-chats-linux/
│   ├── build/
│   │   └── ai_infra_server_mac  # 编译后的可执行文件
│   ├── include/                 # 头文件
│   ├── src/                     # 源代码
│   ├── warmup_templates_code.json
│   └── start_server.sh
├── models/                      # 模型文件（volume 挂载）
│   └── tinyllama-q4.gguf
└── third_party/                 # 第三方依赖
    └── llama.cpp/
```

---

## 卸载清理

```bash
# 停止并删除容器
docker-compose down

# 删除镜像
docker rmi ai-chats-linux:latest

# 删除数据卷（谨慎！会删除所有用户数据）
docker volume rm ai-chats-data

# 清理所有未使用的资源
docker system prune -a --volumes
```

---

## 下一步

1. **前端集成**：访问 `UI/chat.html` 使用 Web 界面
2. **API 文档**：查看 `README.md` 了解完整 API
3. **性能测试**：运行 `benchmark/scripts/test_prefix_cache.py`
4. **研究论文**：阅读 `research/毕设开题报告.md`

---

## 获取帮助

- **项目文档**：`AI-chats-linux/README.md`
- **快速开始**：`AI-chats-linux/QUICKSTART.md`
- **技术细节**：`AI-chats-linux/MIGRATION_NOTES.md`
- **Docker 官方文档**：https://docs.docker.com/

祝使用愉快！
