# Server 2025 - 统一部署与分发指南

## 📋 文档概览

本文档适用于 **Server 2025 统一 Docker 镜像**，包含：
- **6个可运行的服务器**: Server-7, 8, 11, 12, 13, 14
- **统一镜像大小**: 933MB
- **交互式启动**: 进入容器后可选择运行任意服务器
- **模型共享**: LLM 服务器共享同一个模型文件

---

## 🎯 分发流程概览

```
教师端                              学生端
┌─────────────────┐              ┌─────────────────┐
│ 1. Build 镜像   │              │ 1. Pull 镜像    │
│ 2. Test 测试    │              │ 2. 下载模型     │
│ 3. Push 到Hub   │──────────────▶ 3. 运行容器     │
│ 4. 准备分发包   │              │ 4. 测试服务     │
└─────────────────┘              └─────────────────┘
```

---

# 第一部分：教师端操作指南

## 步骤 1: 构建统一 Docker 镜像

### 1.1 进入项目目录

```bash
cd /Users/xiaohuo/Documents/Code/项目一代码/server2025
```

### 1.2 构建镜像

```bash
# 替换 yourusername 为你的 Docker Hub 用户名
docker build -t yourusername/server2025:latest .
```

**预期输出：**
```
[+] Building 180.5s (50/50) FINISHED
...
 => exporting to image
 => => naming to docker.io/yourusername/server2025:latest
```

**构建时间**: 约 3-5 分钟（首次构建，包含 llama.cpp 编译）

### 1.3 验证镜像

```bash
docker images | grep server2025
```

**预期输出：**
```
yourusername/server2025   latest   65c9bf863916   2 minutes ago   933MB
```

---

## 步骤 2: 本地测试镜像

### 2.1 测试非 LLM 服务器（Server-7）

```bash
# 清理旧容器
docker stop test-server7 2>/dev/null || true
docker rm test-server7 2>/dev/null || true

# 运行 Server-7（Router）
docker run -d \
  --name test-server7 \
  -p 8080:8080 \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 7"
```

**等待 2 秒后测试：**

```bash
curl http://localhost:8080/
```

**预期输出：**
```
Hello, World!
```

**清理：**
```bash
docker stop test-server7 && docker rm test-server7
```

✅ **Server-7 测试通过！**

---

### 2.2 测试 LLM 服务器（Server-11）

**⚠️ 前提条件：** 你需要有 GGUF 格式的模型文件

**推荐模型：**
- **名称**: DeepSeek-R1-Distill-Qwen-1.5B
- **文件**: `DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf`
- **大小**: 1.2GB
- **下载**: [HuggingFace](https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF)

**假设模型已下载到 `~/Downloads/`**

```bash
# 清理旧容器
docker stop test-server11 2>/dev/null || true
docker rm test-server11 2>/dev/null || true

# 运行 Server-11（LLM）
docker run -d \
  --name test-server11 \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"
```

**参数说明：**
- `-v ~/Downloads:/app/models:ro` - 挂载模型目录（只读）
- `-e MODEL_PATH=...` - 指定模型文件路径
- `./start-server.sh 13` - 启动 Server-11（菜单编号13）

**查看日志（模型加载需要 1-2 分钟）：**

```bash
docker logs -f test-server11
```

**看到类似输出说明正在加载：**
```
[SimpleInference] Loading model: /app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
load_tensors:   CPU_Mapped model buffer size =  1225.07 MiB
................................................................
[SimpleInference] Model loaded successfully!
```

**测试 API（新开一个终端）：**

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

**预期输出：**
```json
{"success":true,"response":"，我是一个AI助手，很高兴为你服务..."}
```

**清理：**
```bash
docker stop test-server11 && docker rm test-server11
```

✅ **Server-11 测试通过！**

---

### 2.3 测试交互式启动

```bash
# 运行容器进入交互模式
docker run -it --rm \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest

# 进入容器后，运行
./start-server.sh

# 输入编号选择服务器（如输入 7 运行 Server-7）
```

**预期显示菜单：**
```
====================================
   Server 2025 - 统一启动脚本
====================================

可用的 Server 版本：
  1)  Server-1  - Hello World
  ...
  7)  Server-7  - Router 路由系统
  ...
  13) Server-11 - LLM 推理服务 ⭐
  ...
```

✅ **交互式启动测试通过！**

---

## 步骤 3: 推送到 Docker Hub

### 3.1 登录 Docker Hub

```bash
docker login
```

输入你的用户名和密码（或使用 Personal Access Token）。

### 3.2 推送镜像

```bash
docker push yourusername/server2025:latest
```

**预期输出：**
```
The push refers to repository [docker.io/yourusername/server2025]
...
latest: digest: sha256:... size: 4321
```

**推送时间：** 约 10-15 分钟（取决于网速，镜像大小 933MB）

### 3.3 验证推送成功

访问 `https://hub.docker.com/r/yourusername/server2025` 确认镜像已上传。

---

## 步骤 4: 准备模型文件

### 4.1 下载推荐模型

**推荐模型：DeepSeek-R1-Distill-Qwen-1.5B**

| 属性 | 值 |
|------|-----|
| 大小 | 1.2GB |
| 特点 | 中英双语，推理能力强 |
| 下载 | [HuggingFace](https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF) |
| 文件名 | `DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf` |

**下载方法 1: 浏览器下载**

1. 访问上述 HuggingFace 链接
2. 点击 "Files and versions" 标签
3. 找到 `DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf`
4. 点击下载图标

**下载方法 2: 命令行下载（推荐）**

```bash
# 安装 huggingface-cli
pip install huggingface-hub

# 下载模型
huggingface-cli download \
  deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF \
  DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  --local-dir ./models
```

### 4.2 上传模型到网盘

**推荐网盘：**
- 百度网盘（国内推荐）
- 阿里云盘
- OneDrive
- Google Drive

**获取分享链接，记录：**
- 链接地址
- 提取码（如有）
- 文件名：`DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf`
- 文件大小：1.2GB（用于学生验证下载完整性）

---

## 步骤 5: 准备学生分发包

### 5.1 创建分发目录

```bash
mkdir -p ~/server2025-student-package
cd ~/server2025-student-package
```

### 5.2 创建学生使用指南

```bash
cat > STUDENT_GUIDE.md << 'EOF'
# Server 2025 - 学生使用指南

## 快速开始（3步）

### 第1步：拉取 Docker 镜像

```bash
docker pull yourusername/server2025:latest  # 替换为实际用户名
```

**下载大小**: 933MB
**下载时间**: 约 5-10 分钟（取决于网速）

---

### 第2步：下载模型文件（仅 LLM 服务器需要）

**如果你只运行 Server-7 或 Server-8，跳过此步骤**

**模型文件：**
- **网盘链接**: https://pan.baidu.com/s/xxxxx  <!-- 教师替换 -->
- **提取码**: 1234  <!-- 教师替换 -->
- **文件名**: DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
- **文件大小**: 1.2GB

**下载后保存到：**
- **macOS/Linux**: `~/Downloads/`
- **Windows**: `C:\Users\你的用户名\Downloads\`

**验证下载完整性：**
```bash
# macOS/Linux
ls -lh ~/Downloads/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf

# 应该显示约 1.2GB
```

---

### 第3步：运行服务器

根据你需要运行的服务器类型选择：

#### 方式 A：交互式启动（推荐）

**macOS/Linux:**

```bash
# 不需要模型的服务器（Server-7, 8）
docker run -it --rm -p 8080:8080 \
  yourusername/server2025:latest

# 需要模型的服务器（Server-11, 12, 13, 14）
docker run -it --rm -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest
```

**Windows (PowerShell):**

```powershell
# 不需要模型的服务器（Server-7, 8）
docker run -it --rm -p 8080:8080 `
  yourusername/server2025:latest

# 需要模型的服务器（Server-11, 12, 13, 14）
docker run -it --rm -p 8080:8080 `
  -v C:\Users\你的用户名\Downloads:/app/models:ro `
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf `
  yourusername/server2025:latest
```

**进入容器后，运行启动脚本：**

```bash
./start-server.sh
```

**输入服务器编号：**
```
请选择要启动的 Server (输入数字): 13  # 输入编号启动
```

**可用服务器列表：**

| 编号 | 服务器 | 描述 | 需要模型 |
|------|--------|------|----------|
| 7 | Server-7 | Router 路由系统 | ❌ |
| 8 | Server-8 | UI 用户界面 | ❌ |
| 13 | Server-11 | LLM 推理服务 | ✅ |
| 14 | Server-12 | MultiChat 多轮对话 | ✅ |
| 15 | Server-13 | ContextPool 上下文池 | ✅ |
| 18 | Server-14 | DeepSeek-R1 推理链 | ✅ |

---

#### 方式 B：直接启动指定服务器

**Server-7 (Router):**

```bash
# macOS/Linux
docker run -d \
  --name server7 \
  -p 8080:8080 \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 7"

# Windows PowerShell
docker run -d `
  --name server7 `
  -p 8080:8080 `
  yourusername/server2025:latest `
  bash -c "./start-server.sh 7"
```

**Server-11 (LLM):**

```bash
# macOS/Linux
docker run -d \
  --name server11 \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"

# Windows PowerShell
docker run -d `
  --name server11 `
  -p 8080:8080 `
  -v C:\Users\你的用户名\Downloads:/app/models:ro `
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf `
  yourusername/server2025:latest `
  bash -c "./start-server.sh 13"
```

**Server-12 (MultiChat):**

```bash
# 启动命令同 Server-11，只需将最后的 13 改为 14
bash -c "./start-server.sh 14"
```

**Server-13 (ContextPool):**

```bash
# 启动命令同 Server-11，只需将最后的 13 改为 15
bash -c "./start-server.sh 15"
```

**Server-14 (DeepSeek-R1):**

```bash
# 启动命令同 Server-11，只需将最后的 13 改为 18
bash -c "./start-server.sh 18"
```

---

## 第4步：等待模型加载（LLM 服务器）

**如果运行的是 LLM 服务器（11, 12, 13, 14），需要等待模型加载**

查看日志：

```bash
docker logs -f server11  # 替换为你的容器名
```

**看到类似输出说明正在加载：**
```
[SimpleInference] Loading model: /app/models/...
load_tensors:   CPU_Mapped model buffer size =  1225.07 MiB
................................................................
[SimpleInference] Model loaded successfully!
```

**加载时间**: 约 1-2 分钟

---

## 第5步：测试服务

### 测试 Server-7 (Router)

```bash
curl http://localhost:8080/
```

**预期输出：**
```
Hello, World!
```

### 测试 Server-11 (LLM)

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

**预期输出：**
```json
{"success":true,"response":"，我是一个AI助手..."}
```

### 测试 Server-8 (UI)

在浏览器访问：`http://localhost:8080`

---

## 常用命令

### 查看运行中的容器

```bash
docker ps
```

### 查看日志

```bash
docker logs -f server11  # 替换为你的容器名
```

### 停止服务

```bash
docker stop server11  # 替换为你的容器名
```

### 重启服务

```bash
docker restart server11  # 替换为你的容器名
```

### 删除容器

```bash
docker stop server11 && docker rm server11
```

### 切换到另一个服务器

```bash
# 1. 停止并删除当前容器
docker stop server11 && docker rm server11

# 2. 启动新服务器（如 Server-7）
docker run -d \
  --name server7 \
  -p 8080:8080 \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 7"
```

### 使用不同的模型

```bash
# 停止旧容器
docker stop server11 && docker rm server11

# 使用新模型启动
docker run -d \
  --name server11 \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/另一个模型.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"
```

---

## 常见问题

### Q1: Docker 未运行

**错误信息：**
```
Cannot connect to the Docker daemon
```

**解决方法：**
1. 启动 Docker Desktop
2. 等待完全启动（状态栏图标变绿）
3. 重新运行命令

---

### Q2: 模型文件找不到

**错误信息：**
```
⚠️  警告: 模型文件不存在: /app/models/...
```

**解决方法：**

1. **检查文件是否存在：**
   ```bash
   # macOS/Linux
   ls -lh ~/Downloads/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf

   # Windows
   dir C:\Users\你的用户名\Downloads\DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
   ```

2. **检查文件名是否完全一致**（包括大小写）

3. **检查挂载路径是否正确：**
   - macOS/Linux: `-v ~/Downloads:/app/models:ro`
   - Windows: `-v C:/Users/你的用户名/Downloads:/app/models:ro`

---

### Q3: 端口被占用

**错误信息：**
```
Bind for 0.0.0.0:8080 failed: port is already allocated
```

**解决方法：**

使用其他端口：

```bash
# 使用 8081 端口
docker run -d \
  --name server11 \
  -p 8081:8080 \  # 修改这里
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest \
  bash -c "./start-server.sh 13"

# 然后访问 http://localhost:8081
```

---

### Q4: 推理速度很慢

**可能原因：**
1. Docker 内存分配不足
2. 模型太大
3. max_tokens 设置太高

**解决方法：**

1. **增加 Docker 内存分配：**
   - Docker Desktop → Settings → Resources → Memory
   - 建议设置为 4GB 或更高

2. **减少生成长度：**
   ```bash
   # 将 max_tokens 从 128 减少到 32
   -d '{"prompt":"你好", "max_tokens":"32"}'
   ```

---

### Q5: 模型加载失败

**错误信息：**
```
[ERROR] Failed to load model
```

**解决方法：**

1. **检查文件完整性：**
   ```bash
   # 文件大小应该约为 1.2GB
   ls -lh ~/Downloads/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
   ```

2. **如果文件损坏，重新下载**

3. **检查内存是否足够：**
   - Docker Desktop 需要至少 4GB 内存

---

## 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | macOS 10.15+, Windows 10+, Linux |
| Docker | Docker Desktop 最新版 |
| 内存 | 至少 8GB RAM（推荐 16GB） |
| 存储空间 | 至少 3GB 可用空间 |
| CPU | 支持 AVX2 指令集（大部分现代 CPU） |

---

## 服务器功能对比

| 服务器 | 功能 | 适用场景 | 性能 |
|--------|------|----------|------|
| Server-7 | 路由系统 | 学习 HTTP 路由 | 快 |
| Server-8 | UI 界面 | 学习前后端交互 | 快 |
| Server-11 | LLM 推理 | 学习 AI 推理基础 | 中等 |
| Server-12 | 多轮对话 | 学习对话管理 | 中等 |
| Server-13 | 上下文池 | 学习并发优化 | 中等 |
| Server-14 | 推理链 | 学习 DeepSeek-R1 | 中等 |

---

## 进阶使用

### 同时运行多个服务器

```bash
# Server-7 on port 8080
docker run -d --name server7 -p 8080:8080 \
  yourusername/server2025:latest bash -c "./start-server.sh 7"

# Server-11 on port 8081
docker run -d --name server11 -p 8081:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"
```

### 进入容器调试

```bash
# 进入运行中的容器
docker exec -it server11 bash

# 在容器内可以：
# - 查看文件：ls -la
# - 查看模型：ls -lh /app/models/
# - 手动启动服务器：./start-server.sh
```

---

**遇到问题？**
- 查看日志：`docker logs -f <容器名>`
- 联系助教或老师
- 查看项目文档

**祝学习愉快！🎉**
EOF
```

**替换 README 中的占位符：**

编辑 `STUDENT_GUIDE.md`，替换：
- `yourusername` → 你的 Docker Hub 用户名（所有出现的地方）
- 网盘链接和提取码
- 确认模型文件名

---

### 5.3 创建快速参考卡

```bash
cat > QUICK_REFERENCE.md << 'EOF'
# Server 2025 - 快速参考

## 启动命令速查

### Server-7 (Router)
```bash
docker run -d --name server7 -p 8080:8080 \
  yourusername/server2025:latest bash -c "./start-server.sh 7"
curl http://localhost:8080/
```

### Server-8 (UI)
```bash
docker run -d --name server8 -p 8080:8080 \
  yourusername/server2025:latest bash -c "./start-server.sh 8"
# 浏览器访问 http://localhost:8080
```

### Server-11 (LLM)
```bash
docker run -d --name server11 -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"

# 测试
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

## 常用命令

| 操作 | 命令 |
|------|------|
| 查看容器 | `docker ps` |
| 查看日志 | `docker logs -f <容器名>` |
| 停止容器 | `docker stop <容器名>` |
| 删除容器 | `docker rm <容器名>` |
| 重启容器 | `docker restart <容器名>` |

## 问题排查

| 问题 | 解决方法 |
|------|----------|
| Docker 未运行 | 启动 Docker Desktop |
| 端口被占用 | 使用 `-p 8081:8080` |
| 模型找不到 | 检查 `-v` 挂载路径 |
| 内存不足 | Docker 设置增加内存 |

---

**模型下载链接**: <!-- 教师填写 -->
**Docker 镜像**: `yourusername/server2025:latest`
EOF
```

---

### 5.4 打包分发

```bash
cd ~
zip -r server2025-student-package.zip server2025-student-package/
```

或使用 tar：

```bash
tar -czf server2025-student-package.tar.gz server2025-student-package/
```

---

## 步骤 6: 分发给学生

### 6.1 分发方式

1. **邮件发送** `server2025-student-package.zip`
2. **学校网站**上传
3. **U盘**复制
4. **微信/QQ 群**分享

### 6.2 分发内容清单

- ✅ `server2025-student-package.zip` (包含使用指南)
- ✅ 模型下载链接（网盘）
- ✅ Docker Hub 镜像名称：`yourusername/server2025:latest`
- ✅ 联系方式（答疑渠道）

---

## 步骤 7: 课堂演示（10分钟）

### 演示流程

**1. 拉取镜像（2分钟）**

```bash
docker pull yourusername/server2025:latest
docker images | grep server2025
```

**2. 说明模型下载（1分钟）**
- 打开网盘链接
- 说明文件大小（1.2GB）和保存位置

**3. 演示 Server-7（3分钟）**

```bash
# 启动 Server-7
docker run -d --name demo-server7 -p 8080:8080 \
  yourusername/server2025:latest bash -c "./start-server.sh 7"

# 测试
curl http://localhost:8080/
```

**4. 演示 Server-11（4分钟）**

```bash
# 启动 Server-11
docker run -d --name demo-server11 -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"

# 查看日志
docker logs -f demo-server11

# 测试 API
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"什么是人工智能？", "max_tokens":"64"}'
```

---

# 第二部分：学生端操作指南

## 学生快速开始（3步）

### 第1步：拉取镜像

```bash
docker pull yourusername/server2025:latest
```

### 第2步：下载模型（LLM 服务器需要）

按照分发包中 `STUDENT_GUIDE.md` 的说明下载模型文件。

### 第3步：运行服务器

**交互式启动：**

```bash
# 不需要模型
docker run -it --rm -p 8080:8080 yourusername/server2025:latest

# 需要模型
docker run -it --rm -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest

# 进入容器后运行
./start-server.sh
```

**直接启动：**

```bash
# Server-7
docker run -d --name server7 -p 8080:8080 \
  yourusername/server2025:latest bash -c "./start-server.sh 7"

# Server-11
docker run -d --name server11 -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"
```

---

# 第三部分：高级主题

## 使用国内镜像仓库

### 阿里云容器镜像服务

**教师端：**

```bash
# 登录阿里云
docker login --username=你的阿里云账号 registry.cn-hangzhou.aliyuncs.com

# 打标签
docker tag yourusername/server2025:latest \
  registry.cn-hangzhou.aliyuncs.com/你的命名空间/server2025:latest

# 推送
docker push registry.cn-hangzhou.aliyuncs.com/你的命名空间/server2025:latest
```

**学生端：**

```bash
docker pull registry.cn-hangzhou.aliyuncs.com/你的命名空间/server2025:latest

# 重新打标签方便使用
docker tag registry.cn-hangzhou.aliyuncs.com/你的命名空间/server2025:latest \
  server2025:latest
```

---

## 模型挂载详解

### 挂载方式对比

| 方式 | 命令 | 优点 | 缺点 |
|------|------|------|------|
| 只读挂载 | `-v ~/Downloads:/app/models:ro` | 安全，防止误删 | 不能修改模型 |
| 读写挂载 | `-v ~/Downloads:/app/models:rw` | 可以修改 | 可能误删模型 |
| 指定文件 | 不支持 | - | Docker 不支持 |

**推荐使用只读挂载（`:ro`）**

### 多模型管理

**目录结构：**

```
~/models/
├── DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf  (1.2GB)
├── TinyLlama-1.1B-Chat-Q4_K_M.gguf            (650MB)
└── Qwen2.5-0.5B-Instruct-Q4_K_M.gguf          (350MB)
```

**启动时选择模型：**

```bash
# 使用 DeepSeek
docker run -d --name server-deepseek -p 8080:8080 \
  -v ~/models:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"

# 使用 TinyLlama
docker run -d --name server-tinyllama -p 8081:8080 \
  -v ~/models:/app/models:ro \
  -e MODEL_PATH=/app/models/TinyLlama-1.1B-Chat-Q4_K_M.gguf \
  yourusername/server2025:latest bash -c "./start-server.sh 13"
```

---

## 资源估算

### 单个学生

| 项目 | 大小/配置 |
|------|----------|
| Docker 镜像 | 933MB |
| 模型文件 | 1.2GB |
| 运行内存 | 2-4GB |
| 总存储 | ~2.5GB |

### 课程规模

**50 人课程：**
- Docker Hub 流量：933MB × 50 = 46.6GB
- 模型下载：学生自行下载（使用网盘）

**100 人课程：**
- 推荐使用国内镜像仓库（阿里云、腾讯云）
- 或提供学校内网镜像源

---

## 分发前检查清单

教师在分发前确认：

- [ ] Docker 镜像已构建成功（933MB）
- [ ] 镜像已在本地测试通过（Server-7 和 Server-11）
- [ ] 镜像已推送到 Docker Hub
- [ ] 模型文件已上传到网盘
- [ ] 网盘分享链接可用且测试过下载
- [ ] `STUDENT_GUIDE.md` 已更新（用户名、链接）
- [ ] `QUICK_REFERENCE.md` 已更新
- [ ] 在干净环境测试过完整流程
- [ ] 准备好答疑渠道（微信群、邮箱等）
- [ ] 打包好分发包（zip 或 tar.gz）

---

## 故障排查手册

### 教师端常见问题

**Q: 构建失败 - third_party 不存在**

```bash
# 检查目录
ls -la server-11-LLM/third_party/llama.cpp

# 如果不存在，检查 git submodule
git submodule update --init --recursive
```

**Q: 推送速度慢**

使用国内镜像仓库或学校内网。

---

### 学生端常见问题

**Q: 容器启动但 API 不响应**

```bash
# 检查容器状态
docker ps

# 查看日志
docker logs server11

# 如果看到很多点（...），模型还在加载，等待 1-2 分钟
```

**Q: Windows 路径问题**

```powershell
# PowerShell 中使用
-v C:/Users/Student/Downloads:/app/models:ro
# 或
-v C:\Users\Student\Downloads:/app/models:ro
```

**Q: 内存不足**

Docker Desktop → Settings → Resources → Memory → 调整到至少 4GB

---

## 实战测试记录

**测试环境：**
- macOS
- Docker Desktop 最新版
- 模型：DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf (1.2GB)

**测试结果：**
- ✅ 镜像构建成功（933MB）
- ✅ Server-7 启动成功，响应正常
- ✅ Server-11 启动成功，模型加载时间 ~1-2分钟
- ✅ API 调用成功，推理速度正常
- ✅ 交互式启动功能正常
- ✅ 模型挂载机制正常

---

## 更新镜像流程

**如果需要更新镜像（修复 bug、添加功能）：**

```bash
# 1. 修改代码后重新构建
docker build -t yourusername/server2025:v1.1 .

# 2. 同时打上 latest 标签
docker tag yourusername/server2025:v1.1 yourusername/server2025:latest

# 3. 推送两个标签
docker push yourusername/server2025:v1.1
docker push yourusername/server2025:latest

# 4. 通知学生更新
```

**学生更新命令：**

```bash
# 1. 停止并删除旧容器
docker stop server11 && docker rm server11

# 2. 拉取新镜像
docker pull yourusername/server2025:latest

# 3. 重新运行（使用相同的启动命令）
```

---

**准备完毕！现在可以分发给学生了！🎉**
