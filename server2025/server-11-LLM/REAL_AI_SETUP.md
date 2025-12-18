# Server-11 真实AI模型接入配置指南

## 🎯 目标

为 Server-11 配置真实的 llama.cpp AI 推理引擎，支持实际的大语言模型对话。

---

## 📋 方案概述

我们提供了**完整的Docker解决方案**，自动完成：

1. ✅ llama.cpp库的编译
2. ✅ Server-11的编译
3. ✅ TinyLlama模型的下载（可选）
4. ✅ 一键启动服务

---

## 🚀 快速开始（推荐）

### 方法1：使用Docker（最简单）

```bash
# 1. 进入Server-11目录
cd server-11-LLM

# 2. 构建Docker镜像（首次需要10-15分钟）
docker-compose build

# 3. 启动服务
docker-compose up

# 服务将在 http://localhost:8080 启动
```

**首次构建时间**：
- llama.cpp编译：约5分钟
- Server-11编译：约10秒
- TinyLlama模型下载：约3-5分钟（600MB）
- 总计：约10-15分钟

**后续启动时间**：<5秒

---

### 方法2：不下载模型（快速测试）

如果网络较慢或想先测试编译，可以跳过模型下载：

**修改Dockerfile**（注释掉模型下载部分）：
```dockerfile
# 在Dockerfile中找到以下行，注释掉：
# RUN echo "Downloading TinyLlama model..." && \
#     cd /app/models && \
#     wget -O tinyllama-q4.gguf \
#     https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
#     2>&1 | grep -E '(saved|Downloaded|ERROR)' || \
#     echo "Model download failed or skipped - you can mount it manually"
```

然后手动下载模型：
```bash
# 创建models目录
mkdir -p models

# 下载模型
./download_model.sh

# 或使用wget手动下载
wget -O models/tinyllama-q4.gguf \
  https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf
```

---

## 📁 目录结构

```
server-11-LLM/
├── third_party/              # 第三方库
│   └── llama.cpp/           # ✅ 已克隆（从GitHub）
│       ├── include/         # llama.cpp头文件
│       ├── src/             # llama.cpp源代码
│       └── build/           # 编译输出（Docker中生成）
├── models/                   # AI模型目录
│   └── tinyllama-q4.gguf    # TinyLlama模型（600MB）
├── Dockerfile                # ✅ 新增：Docker构建文件
├── docker-compose.yml        # ✅ 新增：Docker编排配置
├── download_model.sh         # ✅ 新增：模型下载脚本
├── SimpleInference.h         # AI推理引擎
├── Router.h                  # 路由配置
├── main.cpp                  # 主程序
└── README.md                 # 原始文档
```

---

## 🧪 测试真实AI推理

### 测试1：简单问答

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is 2+2?", "max_tokens":"64"}'
```

**预期输出**（真实AI生成）：
```json
{
  "success": true,
  "response": "2 + 2 equals 4. This is a basic arithmetic operation."
}
```

### 测试2：代码生成

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Write a Python function to calculate factorial", "max_tokens":"128"}'
```

**预期输出**：
```json
{
  "success": true,
  "response": "def factorial(n):\n    if n == 0 or n == 1:\n        return 1\n    return n * factorial(n - 1)"
}
```

### 测试3：对话（单轮）

```bash
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Explain what is a neural network in one sentence", "max_tokens":"64"}'
```

---

## 📊 性能指标

### 硬件要求

| 配置 | 最低要求 | 推荐配置 |
|------|---------|---------|
| CPU | 2核心 | 4核心+ |
| 内存 | 2GB | 4GB+ |
| 磁盘 | 2GB | 5GB+ |
| 网络 | 下载模型需要 | 稳定连接 |

### 推理性能（TinyLlama-1.1B Q4）

| 指标 | 数值 |
|------|------|
| 模型大小 | ~600MB |
| 加载时间 | ~2-3秒 |
| 首token延迟 | ~100-200ms |
| 生成速度 | ~10-20 tokens/s（CPU） |
| 内存占用 | ~1.5GB |

---

## 🔧 高级配置

### 1. 使用不同的模型

**支持的模型格式**：GGUF格式的量化模型

**推荐模型**：

| 模型 | 大小 | 质量 | 下载链接 |
|------|------|------|----------|
| TinyLlama-1.1B-Q4 | 600MB | ⭐⭐⭐ | [HuggingFace](https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF) |
| Llama-2-7B-Q4 | 3.5GB | ⭐⭐⭐⭐ | [HuggingFace](https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF) |
| Mistral-7B-Q4 | 4GB | ⭐⭐⭐⭐⭐ | [HuggingFace](https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF) |

**更换模型**：
```bash
# 下载新模型到models目录
wget -O models/your-model.gguf https://...

# 修改docker-compose.yml
environment:
  - MODEL_PATH=/app/models/your-model.gguf

# 重启容器
docker-compose restart
```

### 2. 性能优化

**增加线程数**（修改 SimpleInference.h）：
```cpp
llama_context_params ctx_params = llama_context_default_params();
ctx_params.n_ctx = 2048;
ctx_params.n_threads = 8;      // 增加到8线程
ctx_params.n_batch = 1024;     // 增加batch大小
```

**增加Docker资源限制**（修改 docker-compose.yml）：
```yaml
deploy:
  resources:
    limits:
      cpus: '8'        # 增加CPU核心
      memory: 8G       # 增加内存
```

### 3. 持久化配置

**挂载本地模型目录**（避免每次重建容器时重新下载）：
```yaml
# docker-compose.yml
volumes:
  - ./models:/app/models    # 取消注释这行
```

---

## 🐛 故障排查

### 问题1：Docker构建失败

**症状**：`docker-compose build` 报错

**解决方案**：
```bash
# 检查Docker是否运行
docker ps

# 清理旧镜像
docker system prune -a

# 重新构建
docker-compose build --no-cache
```

### 问题2：模型下载失败

**症状**：构建时提示"Model download failed"

**解决方案**：
```bash
# 方式1：注释掉Dockerfile中的模型下载，手动下载
./download_model.sh

# 方式2：使用国内镜像（ModelScope）
# 修改download_model.sh中的MODEL_URL为ModelScope链接
```

### 问题3：推理速度慢

**症状**：生成响应需要很长时间

**解决方案**：
1. 增加线程数（见高级配置）
2. 使用更小的模型（如TinyLlama）
3. 减少max_tokens参数
4. 考虑使用GPU版本（需要CUDA支持）

### 问题4：内存不足

**症状**：容器启动失败，提示OOM

**解决方案**：
```bash
# 使用更小的模型
# 或增加Docker内存限制
# Docker Desktop -> Settings -> Resources -> Memory -> 增加到6GB
```

### 问题5：无法访问端口8080

**症状**：curl连接失败

**解决方案**：
```bash
# 检查容器是否运行
docker-compose ps

# 检查容器日志
docker-compose logs

# 检查端口映射
docker ps | grep server11
```

---

## 📈 与Mock模式对比

| 特性 | Mock模式 | 真实AI模式 |
|------|----------|-----------|
| 编译时间 | <1分钟 | ~10分钟 |
| 镜像大小 | ~500MB | ~2GB |
| 模型文件 | 不需要 | 需要（600MB+） |
| 推理质量 | 固定回复 | 真实生成 |
| 适用场景 | 测试/演示 | 生产使用 |

---

## 🎓 学习要点

通过完成真实AI接入，您已经掌握：

- ✅ llama.cpp的编译和集成
- ✅ GGUF模型的下载和使用
- ✅ Docker多阶段构建
- ✅ 真实大语言模型的推理流程
- ✅ AI服务的性能优化

---

## 🚀 下一步

完成Server-11真实AI接入后，可以继续学习：

**Server-12**：会话管理 + 多轮对话
- 实现Session管理
- 上下文拼接
- KV Cache复用
- 真正的聊天机器人

**Server-13**：CMake构建 + 模块化
- 工程化改造
- 跨平台支持
- 第三方库管理

---

## 📚 参考资源

- [llama.cpp GitHub](https://github.com/ggerganov/llama.cpp)
- [GGUF模型下载](https://huggingface.co/models?library=gguf)
- [Docker官方文档](https://docs.docker.com/)
- [TinyLlama模型](https://github.com/jzhang38/TinyLlama)

---

**配置完成时间**：2025-12-17
**作者**：Claude Code
**版本**：Server-11 真实AI版
