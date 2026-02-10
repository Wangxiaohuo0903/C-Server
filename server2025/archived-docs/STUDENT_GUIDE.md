# Server-11 LLM 推理服务 - 学生使用指南

## 📚 课程介绍

欢迎学习《边缘设备大模型本地部署》课程！

本项目演示如何在本地部署和运行大语言模型（LLM），无需云端API，完全在你的电脑上运行。

## 🎯 学习目标

- 理解大模型推理的基本原理
- 掌握 Docker 容器化部署技术
- 学习模型量化和优化技术
- 实践 HTTP API 服务开发

---

## 📋 前置要求

### 必需软件

1. **Docker Desktop**
   - macOS: https://www.docker.com/products/docker-desktop
   - Windows: https://www.docker.com/products/docker-desktop
   - 至少分配 4GB 内存给 Docker

2. **终端/命令行**
   - macOS: Terminal
   - Windows: PowerShell 或 Git Bash

### 硬件要求

- **内存**: 至少 8GB RAM（推荐 16GB）
- **存储**: 至少 5GB 可用空间（模型 + Docker镜像）
- **CPU**: 现代多核处理器（支持AVX2更佳）

---

## 🚀 快速开始

### 步骤1: 获取 Docker 镜像

**从老师处获取镜像文件（推荐）**

```bash
# 加载镜像文件
docker load < server11-deepseek.tar

# 验证镜像
docker images | grep server11
```

**或者自己构建（可选）**

```bash
cd server-11-LLM
docker build -f Dockerfile.nomodel -t server11-deepseek:latest .
```

### 步骤2: 下载模型文件

**推荐模型（任选一个）**

| 模型 | 大小 | 适用场景 | 下载链接 |
|------|------|----------|----------|
| DeepSeek-R1-Distill-Qwen-1.5B-Q4 | 1.2GB | 中英双语，推理能力强 | [HuggingFace](https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF) |
| TinyLlama-1.1B-Q4 | 637MB | 轻量级，快速推理 | [HuggingFace](https://huggingface.co/TinyLlama/TinyLlama-1.1B-Chat-v1.0-GGUF) |
| SmolLM-360M-Q4 | 200MB | 超轻量，边缘设备 | [HuggingFace](https://huggingface.co/HuggingFaceTB/SmolLM-360M-GGUF) |

**下载后放在你喜欢的位置，例如:**
```
~/Downloads/
~/models/
~/Desktop/
```

### 步骤3: 运行服务

**方法A: 使用启动脚本（最简单）**

```bash
# 赋予执行权限（首次使用）
chmod +x student-run.sh

# 启动服务（替换成你的模型路径）
./student-run.sh ~/Downloads/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
```

**方法B: 直接使用 Docker 命令**

```bash
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf \
  server11-deepseek:latest
```

**参数说明:**
- `-d`: 后台运行
- `--name server11-student`: 容器名称
- `-p 8080:8080`: 端口映射（本地8080 → 容器8080）
- `-v ~/Downloads:/app/models:ro`: 挂载模型目录（只读）
- `-e MODEL_PATH=...`: 指定模型文件路径

### 步骤4: 测试服务

**基础测试:**

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好，请介绍一下你自己", "max_tokens":"64"}'
```

**代码生成测试:**

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"Write a Python function to calculate factorial", "max_tokens":"128"}'
```

---

## 🔄 切换模型

**只需重新运行命令，指定不同的模型文件即可！**

```bash
# 停止当前服务
docker stop server11-student
docker rm server11-student

# 使用新模型启动
docker run -d \
  --name server11-student \
  -p 8080:8080 \
  -v ~/models:/app/models:ro \
  -e MODEL_PATH=/app/models/tinyllama-1.1b-q4.gguf \
  server11-deepseek:latest
```

或使用脚本：

```bash
./student-run.sh ~/models/tinyllama-1.1b-q4.gguf
```

---

## 📊 常用命令

### Docker 容器管理

```bash
# 查看运行中的容器
docker ps

# 查看所有容器（包括停止的）
docker ps -a

# 查看日志（实时）
docker logs -f server11-student

# 查看最近50行日志
docker logs --tail 50 server11-student

# 停止服务
docker stop server11-student

# 启动已停止的服务
docker start server11-student

# 重启服务
docker restart server11-student

# 删除容器
docker rm server11-student

# 查看资源占用
docker stats server11-student
```

### API 测试

```bash
# 基础推理
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你的问题", "max_tokens":"128"}'

# 调整生成长度
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"写一篇关于AI的文章", "max_tokens":"512"}'
```

---

## 🛠️ 高级配置

### 更改端口

如果 8080 端口被占用，可以更改为其他端口：

```bash
# 使用 8081 端口
docker run -d \
  --name server11-student \
  -p 8081:8080 \
  -v ~/Downloads:/app/models:ro \
  -e MODEL_PATH=/app/models/your-model.gguf \
  server11-deepseek:latest

# 访问
curl http://localhost:8081/infer-simple ...
```

### 使用 Docker Compose

创建 `.env` 文件：

```bash
MODEL_DIR=/Users/student/Downloads
MODEL_PATH=/app/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf
```

启动：

```bash
docker-compose -f docker-compose.student.yml up -d
```

停止：

```bash
docker-compose -f docker-compose.student.yml down
```

---

## ❓ 常见问题

### Q1: 提示 "Docker未运行"

**A:** 请先启动 Docker Desktop，等待完全启动后再运行脚本。

### Q2: 模型加载失败

**A:** 检查：
1. 模型文件路径是否正确
2. 文件是否是 `.gguf` 格式
3. 文件是否下载完整（检查文件大小）

### Q3: 推理速度很慢

**A:**
1. 检查 Docker 内存分配（建议 4GB+）
2. 使用较小的模型（如 SmolLM-360M）
3. 减少 `max_tokens` 参数

### Q4: 端口冲突

**A:** 更改端口映射，如 `-p 8081:8080`

### Q5: 内存不足

**A:**
1. 关闭其他应用程序
2. 使用更小的模型
3. 增加 Docker 内存限制

---

## 📖 学习资源

### 相关技术文档

- [Docker 官方文档](https://docs.docker.com/)
- [llama.cpp 项目](https://github.com/ggerganov/llama.cpp)
- [GGUF 格式说明](https://github.com/ggerganov/ggml/blob/master/docs/gguf.md)

### 模型资源

- [HuggingFace 模型库](https://huggingface.co/models?library=gguf)
- [模型量化指南](https://github.com/ggerganov/llama.cpp#quantization)

---

## 💡 实验任务

### 任务1: 模型对比

1. 下载 3 个不同大小的模型
2. 分别运行并测试相同的问题
3. 记录：
   - 推理时间
   - 生成质量
   - 内存占用

### 任务2: API 集成

编写一个 Python 脚本调用 API：

```python
import requests

def ask_llm(prompt, max_tokens=128):
    response = requests.post(
        'http://localhost:8080/infer-simple',
        json={'prompt': prompt, 'max_tokens': str(max_tokens)}
    )
    return response.json()

result = ask_llm("什么是人工智能？")
print(result['response'])
```

### 任务3: 性能优化

尝试调整以下参数观察效果：
- Docker 内存限制
- max_tokens 生成长度
- 不同量化级别的模型（Q4 vs Q8）

---

## 🎓 提交作业

请提交以下内容：

1. **实验报告**
   - 使用的模型
   - 测试的问题和结果
   - 遇到的问题和解决方法

2. **截图**
   - Docker 运行状态
   - API 调用结果
   - 资源监控

3. **代码**（如有）
   - 自己编写的测试脚本
   - 集成示例

---

## 📞 获取帮助

- 查看日志: `docker logs server11-student`
- 查看课程文档: `LLM集成课程文档.md`
- 联系助教或老师

---

## 📄 许可证

本项目仅用于教学目的，请勿用于商业用途。

---

**祝学习愉快！🎉**
