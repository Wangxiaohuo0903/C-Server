# Docker 部署与分发指南 (Server-12 & Server-13)

本文档介绍了如何构建 Docker 镜像并上传，以及终端用户如何拉取镜像、下载模型并运行服务的完整流程。

## 流程概览

1.  **开发者**：构建镜像 (不含模型) -> 推送到 Docker Hub / 私有仓库。
2.  **用户**：下载模型文件 (GGUF) -> 拉取 Docker 镜像 -> 挂载模型并启动容器。

---

## 第一部分：开发者操作 (构建与发布)

### 1. 构建镜像
由于模型文件很大，**不要**将模型 `COPY` 到 Dockerfile 中。确保 `Dockerfile` 仅包含代码和编译环境。

```bash
# 进入目录 (以 Server-13 为例)
cd server-13-KVcache

# 构建镜像 (建议加上版本号)
# 格式: docker build -t <你的仓库名>/<镜像名>:<标签> .
docker build -t myusername/server13-kvcache:v1.0 .
```

### 2. 推送镜像到仓库
登录 Docker Hub 或您的私有仓库，然后推送镜像。

```bash
docker login
docker push myusername/server13-kvcache:v1.0
```

---

## 第二部分：用户操作 (下载与运行)

### 1. 准备模型文件
用户需要自行下载 GGUF 格式的模型文件（例如 DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf）。

*   **存放位置**: 假设用户将模型放在本地目录：`/home/user/ai-models/`

### 2. 启动服务

提供了两种启动方式：**Docker CLI** (命令行) 和 **Docker Compose** (推荐)。

#### 方式 A：使用 Docker CLI 直接运行

用户需要在命令中通过 `-v` 参数将本地模型映射到容器内部。

**运行 Server-13 (KV Cache 版):**

```bash
docker run -d \
  --name ai-server \
  -p 8080:8080 \
  -v /home/user/ai-models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf:/app/model.gguf:ro \
  -e MODEL_PATH=/app/model.gguf \
  --restart unless-stopped \
  myusername/server13-kvcache:v1.0
```

*   `-p 8080:8080`: 将容器的 8080 端口映射到宿主机。
*   `-v <本地路径>:<容器路径>:ro`: 挂载模型文件 (ro代表只读)。
*   `-e MODEL_PATH=...`: 告诉程序在容器内的哪个位置寻找模型。

#### 方式 B：使用 Docker Compose (推荐)

创建一个 `docker-compose.yml` 文件，方便管理配置。

```yaml
version: '3.8'

services:
  chat-server:
    image: myusername/server13-kvcache:v1.0
    container_name: ai_chat_server
    ports:
      - "8080:8080"
    volumes:
      # 格式: /本地模型路径:/容器内路径
      - ./models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf:/app/models/model.gguf:ro
      # 可选：挂载数据库以持久化聊天记录
      - ./data/chat_history.db:/app/users.db
    environment:
      - MODEL_PATH=/app/models/model.gguf
    deploy:
      resources:
        limits:
          cpus: '4.0'
          memory: 4G
    restart: always
```

**启动命令：**
```bash
docker-compose up -d
```

### 3. 访问服务

服务启动后，用户打开浏览器访问：

*   **URL**: `http://localhost:8080/multichat.html`

---

## 常见问题排查

1.  **模型路径错误**
    *   **现象**: 容器启动后立即退出 (`Exited`).
    *   **检查**: 查看日志 `docker logs ai_chat_server`。如果显示 "Model load failed"，请检查 `-v` 挂载路径是否正确。

2.  **端口冲突**
    *   **解决**: 修改映射端口，例如 `-p 9090:8080`。

3.  **性能问题**
    *   建议在 Docker 资源限制中分配至少 2GB 内存和 2 个以上 CPU 核心给容器。
