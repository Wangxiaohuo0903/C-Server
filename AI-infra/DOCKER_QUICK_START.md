# 🐳 Docker快速测试指南（Windows）

## ✅ 可以测试的项目

### AI-chats-linux ✅
- Docker Desktop for Windows 可以运行 Linux 容器
- **完全支持** AI-chats-linux 的所有功能

### AI-chats-mac ❌
- Docker **无法运行** macOS 容器
- 需要物理 Mac 设备或 macOS 虚拟机

---

## 🚀 5分钟快速开始

### 1. 安装 Docker Desktop

下载：https://www.docker.com/products/docker-desktop/

安装后确保Docker正在运行（系统托盘有Docker图标🐳）

### 2. 设置代理（如果需要下载模型）

**方法1**: Docker Desktop设置
```
Settings → Resources → Proxies
✅ Manual proxy configuration
HTTP Proxy: http://127.0.0.1:7890
HTTPS Proxy: http://127.0.0.1:7890
```

### 3. 启动项目

```powershell
# 进入项目目录
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\AI-chats-linux

# 一键启动（首次会自动构建，约5-10分钟）
docker-compose up -d
```

### 4. 查看启动日志

```powershell
docker-compose logs -f ai-server
```

**成功标志**:
```
[Model] loaded ok: /app/models/tinyllama-q4.gguf
Server listening on port 8080...
```

### 5. 测试API

```powershell
# 健康检查
curl http://localhost:8081

# AI推理测试
curl -X POST http://localhost:8081/infer `
  -H "Content-Type: application/json" `
  -d '{\"prompt\": \"写一个Python函数计算斐波那契数列\", \"chat_id\": \"test_001\", \"max_tokens\": 100}'
```

---

## 🧪 运行测试程序

### 进入容器

```powershell
docker-compose exec ai-server /bin/bash
```

### 运行测试

```bash
cd /app/AI-chats-linux/build

# 1. 测试任务分类器
./test_classifier_only
# 输出: 30个prompts的分类结果

# 2. 测试置信度引导
./test_confidence_guide
# 输出: 置信度计算 + Pearson相关系数

# 3. 测试推测式解码
./test_speculative
# 输出: 接受率 + 加速比

# 4. 测试任务感知
./test_task_aware
# 输出: 不同任务类型的性能对比

# 退出容器
exit
```

---

## 📥 准备模型文件

### 方式1: 在Windows下载（推荐）

```powershell
# 返回项目根目录
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra

# 创建模型目录
mkdir models -Force
cd models

# 设置代理
$env:HTTP_PROXY="http://127.0.0.1:7890"
$env:HTTPS_PROXY="http://127.0.0.1:7890"

# 下载 TinyLlama 模型（约600MB）
Invoke-WebRequest -Uri "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf" -OutFile "tinyllama-q4.gguf"
```

### 方式2: 在容器内下载

```bash
docker-compose exec ai-server bash

cd /app/models

# 设置代理（访问Windows的代理）
export HTTP_PROXY="http://host.docker.internal:7890"
export HTTPS_PROXY="http://host.docker.internal:7890"

# 下载模型
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
  -O tinyllama-q4.gguf
```

---

## 🔧 常用命令

```powershell
# 启动服务
docker-compose up -d

# 停止服务
docker-compose down

# 查看日志
docker-compose logs -f

# 重新构建
docker-compose build --no-cache

# 查看状态
docker-compose ps

# 进入容器
docker-compose exec ai-server bash

# 重启服务
docker-compose restart

# 清理所有数据（谨慎！）
docker-compose down -v
```

---

## ❌ 常见问题

### Q1: 端口8081被占用

**错误**: `Bind for 0.0.0.0:8081 failed`

**解决**: 修改 `docker-compose.yml`
```yaml
ports:
  - "8082:8080"  # 改用8082端口
```

### Q2: 模型文件找不到

**错误**: `Model load failed`

**检查**:
```powershell
# 确认模型文件存在
ls C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\models

# 检查容器内的文件
docker-compose exec ai-server ls -la /app/models
```

### Q3: 构建失败

**错误**: `git clone failed` 或 `llama.cpp not found`

**解决**: 手动克隆llama.cpp
```powershell
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra
mkdir third_party -Force
cd third_party

# 设置代理
$env:HTTP_PROXY="http://127.0.0.1:7890"
git clone https://github.com/ggerganov/llama.cpp.git

# 重新构建
cd ..\AI-chats-linux
docker-compose build --no-cache
```

### Q4: 内存不足

**错误**: `Killed` 或容器频繁重启

**解决**: 增加Docker内存限制
- Docker Desktop → Settings → Resources
- Memory: 至少8GB

或修改 `docker-compose.yml`:
```yaml
deploy:
  resources:
    limits:
      memory: 12G  # 增加到12GB
```

---

## 📊 完整测试流程

创建测试脚本 `test_docker.ps1`:

```powershell
# test_docker.ps1

Write-Host "=== AI-chats-linux Docker 完整测试 ===" -ForegroundColor Cyan

# 1. 检查Docker
Write-Host "`n[1/7] 检查Docker状态..." -ForegroundColor Yellow
docker --version
if ($LASTEXITCODE -ne 0) {
    Write-Error "Docker未安装"
    exit 1
}

# 2. 构建镜像
Write-Host "`n[2/7] 构建Docker镜像..." -ForegroundColor Yellow
docker-compose build

# 3. 启动服务
Write-Host "`n[3/7] 启动服务..." -ForegroundColor Yellow
docker-compose up -d

# 4. 等待启动
Write-Host "`n[4/7] 等待服务启动（30秒）..." -ForegroundColor Yellow
Start-Sleep -Seconds 30

# 5. 测试API
Write-Host "`n[5/7] 测试健康检查..." -ForegroundColor Yellow
curl http://localhost:8081

# 6. 测试推理
Write-Host "`n[6/7] 测试AI推理..." -ForegroundColor Yellow
curl -X POST http://localhost:8081/infer `
  -H "Content-Type: application/json" `
  -d '{\"prompt\": \"你好\", \"chat_id\": \"test\", \"max_tokens\": 50}'

# 7. 运行测试程序
Write-Host "`n[7/7] 运行测试程序..." -ForegroundColor Yellow
docker-compose exec -T ai-server /app/AI-chats-linux/build/test_classifier_only

Write-Host "`n✅ 测试完成！" -ForegroundColor Green
Write-Host "查看日志: docker-compose logs -f" -ForegroundColor Cyan
Write-Host "停止服务: docker-compose down" -ForegroundColor Cyan
```

运行测试:
```powershell
cd C:\Users\实习生\Documents\Code\server\C-Server\AI-infra\AI-chats-linux
.\test_docker.ps1
```

---

## 🎯 测试清单

- [ ] Docker Desktop已安装并运行
- [ ] 代理已配置（如需要）
- [ ] 模型文件已下载到 `models/`
- [ ] `docker-compose build` 构建成功
- [ ] `docker-compose up -d` 启动成功
- [ ] `curl http://localhost:8081` 返回 "Hello, World!"
- [ ] AI推理API正常工作
- [ ] 测试程序运行成功

---

## 📈 性能参考

| 环境 | 构建时间 | 启动时间 | 推理速度 |
|------|---------|---------|---------|
| **Docker Desktop** | 5-10分钟 | 20-30秒 | ~90%原生 |
| **WSL2** | 3-5分钟 | 5-10秒 | ~95%原生 |
| **物理Linux** | 2-3分钟 | <5秒 | 100% |

---

## 💡 最终建议

### 对于 AI-chats-linux ✅
**推荐顺序**:
1. **Docker** - 最简单，一键启动
2. **WSL2** - 性能更好，更接近原生
3. **物理Linux** - 最佳性能

### 对于 AI-chats-mac ❌
**唯一选择**:
- 需要物理 Mac 设备（MacBook/Mac Mini/iMac）
- 或借用/租用 Mac 云主机

### 功能对齐验证
由于两个版本功能已100%对齐，可以：
1. 在 Docker/WSL2 中测试 **AI-chats-linux**
2. 验证所有功能正常
3. 确认在 Mac 上编译 **AI-chats-mac** 时应该也正常

---

**环境**: Windows 11 + Docker Desktop
**推荐配置**: 16GB RAM, 4核CPU, 20GB磁盘
**预计时间**: 首次15分钟，后续<1分钟
