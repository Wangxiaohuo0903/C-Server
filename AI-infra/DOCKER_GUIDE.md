# 🐳 使用Docker在Windows测试AI-chats-linux

## ⚠️ 重要说明

### ✅ 可以测试：AI-chats-linux
Docker Desktop for Windows 可以运行 **Linux容器**，因此可以测试 **AI-chats-linux**

### ❌ 不能测试：AI-chats-mac
Docker 无法运行 **macOS容器**（需要macOS主机），因此 **AI-chats-mac** 无法通过Docker测试

**原因**:
- Docker主要支持Linux容器
- macOS容器需要macOS内核和硬件支持
- AI-chats-mac使用Metal GPU，Docker无法模拟

**Mac版本测试方案**:
- 需要物理Mac设备
- 或使用macOS虚拟机（需要macOS主机）

---

## 🚀 快速开始

### 1. 安装Docker Desktop

下载并安装：https://www.docker.com/products/docker-desktop/

**检查安装**:
```powershell
docker --version
docker-compose --version
```

### 2. 启动Docker Desktop

确保Docker Desktop正在运行（系统托盘有Docker图标）

### 3. 配置代理（可选）

如果需要访问外网（下载模型）：

**方法1**: Docker Desktop设置
- 打开Docker Desktop
- Settings → Resources → Proxies
- 启用 "Manual proxy configuration"
- HTTP Proxy: `http://127.0.0.1:7890`
- HTTPS Proxy: `http://127.0.0.1:7890`

**方法2**: 修改 docker-compose.yml
```yaml
environment:
  - HTTP_PROXY=http://host.docker.internal:7890
  - HTTPS_PROXY=http://host.docker.internal:7890
```

### 4. 准备项目

```powershell
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\AI-chats-linux
```

### 5. 构建Docker镜像

```powershell
# 方法1: 使用docker-compose（推荐）
docker-compose build

# 方法2: 直接使用docker
docker build -t ai-chats-linux .
```

**预计时间**: 首次构建约5-10分钟

**预期输出**:
```
[+] Building 300.2s (12/12) FINISHED
 => [1/10] FROM ubuntu:22.04
 => [2/10] RUN apt-get update && apt-get install...
 => [3/10] COPY . /app/
 => [4/10] RUN git submodule update...
 => [5/10] RUN mkdir -p build && cd build && cmake && make
 => exporting to image
Successfully built ai-chats-linux
```

### 6. 下载模型

**在Windows中下载**（推荐，更快）:
```powershell
# 创建模型目录
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra
mkdir models -Force
cd models

# 设置代理
$env:HTTP_PROXY="http://127.0.0.1:7890"
$env:HTTPS_PROXY="http://127.0.0.1:7890"

# 下载TinyLlama模型（约600MB）
Invoke-WebRequest -Uri "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf" -OutFile "tinyllama-q4.gguf"
```

### 7. 运行容器

```powershell
# 方法1: 使用docker-compose
docker-compose up

# 方法2: 直接运行
docker run -p 8080:8080 -v "C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\models:/app/models" ai-chats-linux
```

**预期输出**:
```
[Model] loaded ok: /app/models/tinyllama-q4.gguf
Server listening on port 8080...
```

### 8. 测试API

**打开新的PowerShell窗口**:

```powershell
# 健康检查
curl http://localhost:8080

# 测试推理
curl -X POST http://localhost:8080/infer `
  -H "Content-Type: application/json" `
  -d '{\"prompt\": \"Write a Python function\", \"chat_id\": \"test_001\", \"max_tokens\": 100}'
```

---

## 🧪 运行测试程序

### 方法1: 进入容器执行

```powershell
# 进入运行中的容器
docker exec -it ai-chats-linux bash

# 在容器内运行测试
cd /app/build

# 测试1: 任务分类器
./test_classifier_only

# 测试2: 置信度引导
./test_confidence_guide

# 测试3: 推测式解码
./test_speculative

# 测试4: 任务感知
./test_task_aware
```

### 方法2: 使用docker-compose运行测试

```powershell
# 运行测试profile
docker-compose --profile test run test-runner
```

### 方法3: 一次性执行命令

```powershell
docker exec ai-chats-linux /app/build/test_classifier_only
docker exec ai-chats-linux /app/build/test_confidence_guide
docker exec ai-chats-linux /app/build/test_task_aware
```

---

## 📊 性能对比

| 环境 | 推理速度 | 编译速度 | 启动速度 |
|------|---------|---------|---------|
| **物理Linux** | 100% | 100% | 100% |
| **WSL2** | ~95% | ~85% | ~90% |
| **Docker Desktop** | ~90% | ~80% | ~95% |

**结论**: Docker性能略低于WSL2，但隔离性更好

---

## 🔧 常见问题

### Q1: Docker构建失败

**错误**: `git submodule update failed`

**解决**:
```powershell
# 手动克隆llama.cpp
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra
mkdir third_party -Force
cd third_party
git clone https://github.com/ggerganov/llama.cpp.git

# 重新构建
cd ..\AI-chats-linux
docker-compose build --no-cache
```

### Q2: 端口被占用

**错误**: `Bind for 0.0.0.0:8080 failed: port is already allocated`

**解决**:
```powershell
# 修改docker-compose.yml的端口映射
ports:
  - "8081:8080"  # 使用8081端口

# 或者停止占用8080的进程
netstat -ano | findstr :8080
taskkill /PID <进程ID> /F
```

### Q3: 模型加载失败

**错误**: `Model load failed: No such file or directory`

**检查**:
```powershell
# 确认模型文件存在
ls C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\models

# 检查Docker卷挂载
docker exec ai-chats-linux ls -la /app/models
```

**解决**: 确保docker-compose.yml中的卷挂载路径正确

### Q4: 代理无法访问

**错误**: 容器内无法访问Windows的代理

**解决**:
```yaml
# 在docker-compose.yml中使用host.docker.internal
environment:
  - HTTP_PROXY=http://host.docker.internal:7890
  - HTTPS_PROXY=http://host.docker.internal:7890

extra_hosts:
  - "host.docker.internal:host-gateway"
```

### Q5: 内存不足

**错误**: `Killed` 或 `Out of memory`

**解决**:
- 打开Docker Desktop → Settings → Resources
- 增加内存限制到8GB+
- 或修改docker-compose.yml:
```yaml
deploy:
  resources:
    limits:
      memory: 8G
```

---

## 🎯 优化建议

### 1. 使用.dockerignore

创建 `AI-chats-linux/.dockerignore`:
```
build/
*.log
*.db
.git/
```

### 2. 多阶段构建（优化镜像大小）

```dockerfile
# 编译阶段
FROM ubuntu:22.04 as builder
RUN apt-get update && apt-get install -y build-essential cmake
COPY . /app
RUN cd /app && mkdir build && cd build && cmake .. && make

# 运行阶段
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libsqlite3-0
COPY --from=builder /app/build /app/build
CMD ["/app/build/ai_infra_server_mac"]
```

### 3. 使用Docker缓存

```powershell
# 启用BuildKit
$env:DOCKER_BUILDKIT=1
docker-compose build
```

---

## 📝 完整测试脚本

创建 `test_docker.ps1`:

```powershell
# test_docker.ps1 - 一键Docker测试脚本

Write-Host "=== 1. 检查Docker状态 ===" -ForegroundColor Green
docker --version
if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker未安装或未启动"
    exit 1
}

Write-Host "`n=== 2. 构建镜像 ===" -ForegroundColor Green
docker-compose build

Write-Host "`n=== 3. 启动容器 ===" -ForegroundColor Green
docker-compose up -d

Write-Host "`n=== 4. 等待服务启动... ===" -ForegroundColor Green
Start-Sleep -Seconds 5

Write-Host "`n=== 5. 测试API ===" -ForegroundColor Green
curl http://localhost:8080

Write-Host "`n=== 6. 运行测试程序 ===" -ForegroundColor Green
docker exec ai-chats-linux /app/build/test_classifier_only
docker exec ai-chats-linux /app/build/test_confidence_guide

Write-Host "`n=== 7. 查看日志 ===" -ForegroundColor Green
docker-compose logs --tail=50

Write-Host "`n✅ 测试完成！" -ForegroundColor Green
Write-Host "停止容器: docker-compose down" -ForegroundColor Yellow
```

使用方法:
```powershell
.\test_docker.ps1
```

---

## 🔄 常用命令

```powershell
# 启动
docker-compose up -d

# 查看日志
docker-compose logs -f

# 停止
docker-compose down

# 重新构建
docker-compose build --no-cache

# 进入容器
docker exec -it ai-chats-linux bash

# 查看容器状态
docker ps

# 清理所有容器
docker-compose down -v
```

---

## 📈 Docker vs WSL2 vs 物理机

| 特性 | Docker | WSL2 | 物理Linux |
|------|--------|------|-----------|
| **安装难度** | ⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **性能** | 90% | 95% | 100% |
| **隔离性** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐ |
| **便携性** | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐ |
| **GPU支持** | ❌ | ✅ (部分) | ✅ |

**推荐**:
- **开发测试**: WSL2
- **生产部署**: Docker
- **性能测试**: 物理机

---

## 🎓 总结

### ✅ Docker可以做什么

- ✅ 测试 AI-chats-linux
- ✅ 运行所有测试程序
- ✅ 验证功能正确性
- ✅ 简化环境配置
- ✅ 便于分享和部署

### ❌ Docker不能做什么

- ❌ 测试 AI-chats-mac（需要macOS）
- ❌ 使用Metal GPU（需要Apple硬件）
- ❌ 测试kqueue性能（Docker使用Linux内核）

### 💡 建议

**对于你的情况**:
1. **开发**: 使用 WSL2 测试 AI-chats-linux
2. **部署**: 使用 Docker 打包发布
3. **Mac版本**: 需要物理Mac或借用Mac设备

---

**环境**: Windows 11 + Docker Desktop
**推荐配置**: 16GB RAM, 4+ CPU cores, 20GB+ 磁盘空间
**预计用时**: 首次构建10分钟，后续启动30秒
