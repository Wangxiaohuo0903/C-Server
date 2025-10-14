# Mac 版本快速开始指南

## 5 分钟快速上手

### 前置要求

- macOS 12.0+ (Monterey 或更新)
- 已安装 Xcode Command Line Tools
- 至少 8GB 可用内存

### 步骤 1: 安装依赖

```bash
# 安装 Xcode Command Line Tools
xcode-select --install

# 安装 CMake (如果未安装)
brew install cmake
```

### 步骤 2: 编译项目

```bash
# 进入 Mac 版本目录
cd AI-chats-mac

# 使用自动构建脚本
./build.sh
```

编译成功后会显示：

```
================================
编译成功！
================================

可执行文件位置: /path/to/AI-infra/AI-chats-mac/build/ai_infra_server_mac
文件大小: 212K
架构: Mach-O 64-bit executable arm64
```

### 步骤 3: 准备模型文件

选项 A - 下载小型测试模型 (推荐新手):

```bash
# 创建 models 目录
cd ..
mkdir -p models

# 下载 TinyLlama 1.1B Q4 模型 (约 700MB)
curl -L -o models/tinyllama-1.1b-chat-q4.gguf \
  "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"
```

选项 B - 使用你自己的 GGUF 模型:

```bash
# 将你的模型文件复制到 models 目录
cp /path/to/your-model.gguf models/
```

### 步骤 4: 配置模型路径

编辑 `AI-chats-mac/src/main.cpp` 第 11 行：

```cpp
// 修改为你的模型文件名
const std::string modelPath = "../models/tinyllama-1.1b-chat-q4.gguf";
```

重新编译：

```bash
cd AI-chats-mac
./build.sh
```

### 步骤 5: 启动服务器

```bash
cd build
./ai_infra_server_mac
```

看到以下输出表示启动成功：

```
[Model] loaded ok: ../models/tinyllama-1.1b-chat-q4.gguf
Server listening on port 8080...
```

### 步骤 6: 测试 API

打开新的终端窗口，运行测试脚本：

```bash
cd AI-chats-mac
./test_api.sh
```

或者手动测试：

```bash
# 测试根路由
curl http://localhost:8080/

# 测试 AI 推理
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Hello, what is 2+2?","chat_id":"test"}'
```

成功响应示例：

```json
{
  "answer": "2 + 2 equals 4."
}
```

## 常见问题

### Q1: 编译失败 - "sqlite3 not found"

A: Mac 系统自带 SQLite3，如果报错可以尝试：

```bash
brew install sqlite3
cd AI-chats-mac/build
cmake -DSQLITE3_INCLUDE_DIR=/usr/local/include ..
make -j$(sysctl -n hw.ncpu)
```

### Q2: 模型加载失败

A: 检查以下几点：

1. 模型文件路径是否正确
2. 模型文件是否是 GGUF 格式
3. 是否有足够的内存（至少 8GB）

查看详细日志：

```bash
./ai_infra_server_mac 2>&1 | tee server.log
```

### Q3: 端口 8080 被占用

A: 终止占用端口的进程或修改端口：

```bash
# 查找占用进程
lsof -i :8080

# 终止进程
kill -9 <PID>

# 或修改 src/main.cpp 第 19 行的端口号
```

### Q4: Metal GPU 加速未启用

A: 检查是否是 Apple Silicon Mac：

```bash
uname -m
# 输出 arm64 表示 Apple Silicon
# 输出 x86_64 表示 Intel Mac (不支持 Metal)

# 重新编译并启用 Metal
cd AI-chats-mac/build
cmake -DWITH_METAL=ON ..
make -j$(sysctl -n hw.ncpu)
```

### Q5: 推理速度很慢

A: 优化建议：

1. 使用更小的模型（如 TinyLlama）
2. 减小 context 长度（修改 `main.cpp` 第 13 行 `n_ctx`）
3. 减少生成的 token 数量（修改 `HttpServer.h` 第 66 行 `maxTokens`）
4. 确认 Metal GPU 加速已启用（仅 Apple Silicon）

## 下一步

- 阅读完整文档: [README.md](README.md)
- 了解技术细节: [MIGRATION_NOTES.md](MIGRATION_NOTES.md)
- 试用更大的模型（Llama-2-7B, Mistral-7B 等）
- 构建自己的前端界面

## 推荐的模型下载

| 模型 | 大小 | 性能 | 适用场景 |
|------|------|------|----------|
| [TinyLlama-1.1B-Q4](https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF) | ~700MB | ⭐⭐⭐ | 测试、学习 |
| [Llama-2-7B-Q4](https://huggingface.co/TheBloke/Llama-2-7B-Chat-GGUF) | ~4GB | ⭐⭐⭐⭐ | 通用对话 |
| [Mistral-7B-Q4](https://huggingface.co/TheBloke/Mistral-7B-Instruct-v0.2-GGUF) | ~4GB | ⭐⭐⭐⭐⭐ | 高质量输出 |
| [CodeLlama-7B-Q4](https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF) | ~4GB | ⭐⭐⭐⭐ | 代码生成 |

## 性能参考

测试环境: Apple M1 Pro (16GB)

| 模型 | 加载时间 | 首个 token | Tokens/s |
|------|---------|-----------|----------|
| TinyLlama-1.1B-Q4 | ~2s | ~500ms | ~40 |
| Llama-2-7B-Q4 | ~8s | ~1.2s | ~15 |
| Mistral-7B-Q4 | ~8s | ~1.3s | ~12 |

## 获取帮助

如果遇到问题：

1. 查看 [README.md](README.md) 完整文档
2. 检查 [MIGRATION_NOTES.md](MIGRATION_NOTES.md) 已知问题
3. 查看服务器日志: `cat server.log`
4. 提交 Issue 到项目仓库

祝使用愉快！
