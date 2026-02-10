# Server-11 教师分发指南（实战版）

## 📋 分发流程概览

```
教师端                              学生端
┌─────────────────┐              ┌─────────────────┐
│ 1. Build 镜像   │              │ 1. Pull 镜像    │
│ 2. Push 到Hub   │──────────────▶ 2. 下载模型     │
│ 3. 分享模型     │              │ 3. 运行容器     │
└─────────────────┘              └─────────────────┘
```

---

## 🎯 教师端操作流程（实战测试通过）

### 步骤 1: 构建 Docker 镜像

**进入项目目录：**

```bash
cd /Users/xiaohuo/Documents/Code/项目一代码/server2025/server-11-LLM
```

**构建镜像：**

```bash
# 替换 yourusername 为你的 Docker Hub 用户名
docker build -t yourusername/ai-server2026:latest .
```

**预期输出：**
```
[+] Building 45.2s (16/16) FINISHED
...
 => exporting to image
 => => naming to docker.io/yourusername/ai-server2026:latest
```

**验证镜像（大小约 670MB）：**

```bash
docker images | grep ai-server2026
```

**预期输出：**
```
yourusername/ai-server2026   latest   9be9c42779eb   2 minutes ago   669MB
```

---

### 步骤 2: 测试镜像（重要！）

**在推送到 Docker Hub 之前，先本地测试！**

```bash
# 停止旧容器（如果有）
docker stop server11-student 2>/dev/null || true
docker rm server11-student 2>/dev/null || true

# 运行测试（替换为你的模型路径）
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

**等待模型加载（约1-2分钟）：**

```bash
# 查看日志，等待模型加载完成
docker logs -f server11-student
```

**看到类似输出说明正在加载：**
```
load_tensors:   CPU_Mapped model buffer size =  1225.07 MiB
................................................................
```

**测试 API（新开一个终端）：**

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

**预期输出（成功）：**
```json
{"success":true,"response":"，我是一个AI助手..."}
```

---

### 步骤 3: 推送到 Docker Hub

**登录 Docker Hub：**

```bash
docker login
```

输入你的用户名和密码（或使用 access token）。

**推送镜像：**

```bash
docker push yourusername/ai-server2026:latest
```

**预期输出：**
```
The push refers to repository [docker.io/yourusername/ai-server2026]
...
latest: digest: sha256:... size: 1234
```

**推送时间：** 约 5-10 分钟（取决于网速）

**验证推送成功：**

访问 `https://hub.docker.com/r/yourusername/ai-server2026` 确认镜像已上传。

---

### 步骤 4: 准备模型文件

**推荐模型：DeepSeek-R1-Distill-Qwen-1.5B**

| 属性 | 值 |
|------|-----|
| 大小 | 1.2GB |
| 特点 | 中英双语，推理能力强 |
| 下载 | [HuggingFace](https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF) |
| 文件名 | `DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf` |

**下载方法1: 浏览器下载**

1. 访问上述 HuggingFace 链接
2. 点击 "Files and versions" 标签
3. 找到 `DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf`
4. 点击下载图标

**下载方法2: 命令行下载（推荐）**

```bash
# 安装 huggingface-cli
pip install huggingface-hub

# 下载模型
huggingface-cli download \
  deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF \
  DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  --local-dir ./models
```

**上传模型到网盘：**

- 百度网盘（推荐）
- 阿里云盘
- OneDrive
- Google Drive

**获取分享链接，记录：**
- 链接地址
- 提取码（如有）
- 文件名
- 文件大小（用于学生验证下载完整性）

---

### 步骤 5: 准备学生分发包

**创建分发目录：**

```bash
mkdir -p ~/server11-student-package
cd ~/server11-student-package
```

**创建学生说明文档 (README.md)：**

```bash
cat > README.md << 'EOF'
# Server-11 LLM 推理服务 - 学生使用指南

## 快速开始（3步）

### 第1步：下载模型文件

- **网盘链接**: https://pan.baidu.com/s/xxxxx  <!-- 教师替换 -->
- **提取码**: 1234  <!-- 教师替换 -->
- **文件名**: DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
- **文件大小**: 1.2GB

下载后保存到：
- macOS/Linux: `~/Downloads/`
- Windows: `C:\Users\你的用户名\Downloads\`

### 第2步：拉取 Docker 镜像

```bash
docker pull yourusername/ai-server2026:latest  <!-- 教师替换用户名 -->
```

### 第3步：运行服务

**macOS/Linux:**

```bash
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest  <!-- 教师替换用户名 -->
```

**Windows (PowerShell):**

```powershell
docker run -d `
  --name server11-student `
  -p 8080:8080 `
  -v C:\Users\你的用户名\Downloads:/app/models:ro `
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf `
  yourusername/ai-server2026:latest  <!-- 教师替换用户名 -->
```

### 第4步：等待模型加载

模型加载需要 1-2 分钟，查看日志：

```bash
docker logs -f server11-student
```

看到许多点（...）说明正在加载，等待完成。

### 第5步：测试 API

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

**预期输出:**

```json
{"success":true,"response":"...AI生成的回复..."}
```

## 常用命令

### 查看日志
```bash
docker logs -f server11-student
```

### 停止服务
```bash
docker stop server11-student
```

### 重启服务
```bash
docker restart server11-student
```

### 删除容器
```bash
docker stop server11-student
docker rm server11-student
```

### 切换模型
```bash
# 1. 停止并删除旧容器
docker stop server11-student && docker rm server11-student

# 2. 使用新模型启动
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/另一个模型.gguf \
  yourusername/ai-server2026:latest
```

## 常见问题

### Q1: Docker 未运行
**A:** 启动 Docker Desktop，等待完全启动后再运行。

### Q2: 模型加载失败
**A:**
1. 检查文件路径是否正确
2. 检查文件大小是否为 1.2GB
3. 查看详细日志：`docker logs server11-student`

### Q3: 端口被占用
**A:** 使用其他端口：`-p 8081:8080`，然后访问 `http://localhost:8081`

### Q4: 推理速度慢
**A:**
1. 增加 Docker 内存分配（Docker Desktop 设置）
2. 减少 `max_tokens` 参数
3. 使用更小的模型

## 系统要求

- Docker Desktop 最新版
- 内存：至少 8GB RAM
- 存储：至少 3GB 可用空间

---

**遇到问题联系助教或老师**
EOF
```

**注意：** 编辑 `README.md`，替换以下内容：
- `yourusername` -> 你的 Docker Hub 用户名
- 网盘链接和提取码
- 确认模型文件名

**打包分发：**

```bash
cd ~
zip -r server11-student-package.zip server11-student-package/
```

或者：

```bash
tar -czf server11-student-package.tar.gz server11-student-package/
```

---

### 步骤 6: 分发给学生

**分发方式：**

1. **邮件发送** `server11-student-package.zip`
2. **学校网站**上传
3. **U盘**复制
4. **微信/QQ 群**分享

**分发内容：**
- ✅ `server11-student-package.zip` (压缩包)
- ✅ 模型下载链接（网盘）
- ✅ Docker Hub 镜像名称

---

## 👨‍🎓 学生端操作流程（3步）

学生收到分发包后：

### 第1步：下载模型

按照 README.md 中的网盘链接下载模型文件

### 第2步：拉取镜像

```bash
docker pull yourusername/ai-server2026:latest
```

### 第3步：运行服务

```bash
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

### 第4步：测试

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

---

## 🔧 故障排查

### 教师端问题

**Q: 构建失败，提示找不到 third_party**
```bash
# 检查 third_party 目录是否存在
ls -la third_party/llama.cpp

# 如果不存在，需要初始化子模块
git submodule update --init --recursive
```

**Q: 推送到 Docker Hub 速度慢**

使用国内镜像仓库（阿里云、腾讯云）：

```bash
# 阿里云示例
docker tag yourusername/ai-server2026:latest \
  registry.cn-hangzhou.aliyuncs.com/yourusername/ai-server2026:latest

docker push registry.cn-hangzhou.aliyuncs.com/yourusername/ai-server2026:latest
```

### 学生端问题

**Q: 容器启动但 API 不响应**

```bash
# 检查容器状态
docker ps | grep server11-student

# 查看日志
docker logs server11-student

# 如果看到很多点（...），说明模型还在加载，再等1-2分钟
```

**Q: 模型路径错误**

Windows 路径需要特别注意：

```powershell
# PowerShell 中使用反斜杠转义或正斜杠
-v C:/Users/Student/Downloads:/app/models:ro
```

**Q: 内存不足**

Docker Desktop 设置中增加内存限制：
- Settings -> Resources -> Memory -> 至少 4GB

---

## 📊 资源估算

### 单个学生

| 项目 | 大小/配置 |
|------|----------|
| Docker 镜像 | 669MB |
| 模型文件 | 1.2GB |
| 运行内存 | 2-4GB |
| 总存储 | ~2GB |

### 课程规模

**50 人课程：**
- Docker Hub 流量：669MB × 50 = 33GB
- 模型下载：学生自行下载（使用网盘）

**100 人课程：**
- 推荐使用国内镜像仓库
- 或提供学校内网镜像源

---

## ✅ 分发前检查清单

教师在分发前确认：

- [ ] Docker 镜像已构建成功
- [ ] 镜像已在本地测试通过
- [ ] 镜像已推送到 Docker Hub
- [ ] 模型文件已上传到网盘
- [ ] 网盘分享链接可用
- [ ] README.md 已更新（用户名、链接）
- [ ] 在干净环境测试过完整流程
- [ ] 准备好答疑渠道

---

## 🎓 课堂演示建议（10分钟）

### 演示流程

1. **展示镜像拉取**（2分钟）
   ```bash
   docker pull yourusername/ai-server2026:latest
   docker images
   ```

2. **演示模型下载**（1分钟）
   - 打开网盘链接
   - 说明文件大小和保存位置

3. **运行服务**（3分钟）
   ```bash
   docker run -d \
     --name server11-student \
     -p 8080:8080 \
     -v ~/Downloads:/app/models:ro \
     -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
     yourusername/ai-server2026:latest

   # 查看日志
   docker logs -f server11-student
   ```

4. **测试 API**（3分钟）
   ```bash
   curl -X POST http://localhost:8080/infer-simple \
     -H 'Content-Type: application/json' \
     -d '{"prompt":"什么是人工智能？", "max_tokens":"64"}'
   ```

5. **查看日志**（1分钟）
   ```bash
   docker logs server11-student
   ```

---

## 🔄 更新镜像流程

如果需要更新镜像（修复 bug、添加功能）：

```bash
# 1. 修改代码后重新构建
docker build -t yourusername/ai-server2026:v1.1 .

# 2. 同时打上 latest 标签
docker tag yourusername/ai-server2026:v1.1 yourusername/ai-server2026:latest

# 3. 推送两个标签
docker push yourusername/ai-server2026:v1.1
docker push yourusername/ai-server2026:latest

# 4. 通知学生更新
```

学生更新命令：

```bash
# 1. 停止并删除旧容器
docker stop server11-student
docker rm server11-student

# 2. 拉取新镜像
docker pull yourusername/ai-server2026:latest

# 3. 重新运行
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  yourusername/ai-server2026:latest
```

---

## 📝 实战测试记录

**测试环境：**
- macOS
- Docker Desktop 最新版
- 模型：DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf (1.2GB)

**测试结果：**
- ✅ 镜像构建成功（669MB）
- ✅ 容器启动成功
- ✅ 模型加载时间：~1-2分钟
- ✅ API 调用成功
- ✅ 推理速度：正常

**注意事项：**
1. 模型加载需要耐心等待
2. 日志中的点（...）表示加载进度
3. 即使日志看起来卡住，服务可能已经启动
4. 建议等待 1-2 分钟后再测试 API

---

**准备完毕！现在可以分发给学生了！🎉**
