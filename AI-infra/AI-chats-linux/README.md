# AI-Infra Server - Mac 版本

这是原 Linux 版本的 HTTP 服务器 + 本地大语言模型推理项目的 Mac 移植版本。

## 主要改动

### 1. I/O 多路复用机制
- **Linux 版本**: 使用 `epoll` (Linux 特有)
- **Mac 版本**: 使用 `kqueue` (BSD/Mac 标准)

### 2. 编译器兼容性
- 修复 `std::result_of` (C++17) -> `std::invoke_result` (C++20)
- 针对 Apple Silicon (M1/M2/M3) 和 Intel Mac 优化编译选项

### 3. GPU 加速
- **Linux 版本**: 可选 CUDA/CUBLAS
- **Mac 版本**: 使用 Metal GPU 加速 (Apple GPU)

## 系统要求

- **操作系统**: macOS 12.0+ (Monterey 或更新版本)
- **处理器**:
  - Apple Silicon (M1/M2/M3/M4) - 推荐
  - Intel x86_64
- **内存**: 至少 8GB RAM (推荐 16GB+)
- **工具链**:
  - Xcode Command Line Tools
  - CMake 3.23+
  - SQLite3 (通常系统自带)

## 安装依赖

```bash
# 安装 Xcode Command Line Tools
xcode-select --install

# 安装 Homebrew (如果尚未安装)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装 CMake
brew install cmake

# 验证 SQLite3 (Mac 系统自带)
sqlite3 --version
```

## 编译步骤

```bash
# 1. 进入 Mac 版本目录
cd AI-chats-mac

# 2. 创建 build 目录并进入
mkdir -p build && cd build

# 3. 配置 CMake
cmake ..

# 可选：如果要禁用 Metal GPU 加速
cmake -DWITH_METAL=OFF ..

# 4. 编译 (使用所有 CPU 核心)
make -j$(sysctl -n hw.ncpu)

# 5. 查看编译结果
ls -lh ai_infra_server_mac
```

编译成功后，你会在 `build` 目录下看到 `ai_infra_server_mac` 可执行文件。

## 准备模型文件

在运行服务器之前，需要准备一个 GGUF 格式的量化模型：

```bash
# 在项目根目录创建 models 目录
cd ..  # 返回到 AI-infra 根目录
mkdir -p models

# 下载模型示例 (以 TinyLlama 为例)
# 你可以从 Hugging Face 下载任何 GGUF 格式的模型
# 例如: https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF

# 将模型文件放到 models 目录
# mv your-model.gguf models/
```

**推荐的模型下载来源：**
- [Hugging Face - GGUF Models](https://huggingface.co/models?sort=trending&search=gguf)
- TinyLlama (1.1B) - 适合测试，占用内存小
- Llama-2-7B (Q4) - 平衡性能和质量
- Mistral-7B (Q4) - 高质量输出

## 修改模型路径

编辑 `src/main.cpp` 第 11 行，修改为你的模型路径：

```cpp
const std::string modelPath = "../models/your-model-name.gguf";
```

或者使用绝对路径：

```cpp
const std::string modelPath = "/Users/你的用户名/Documents/Code/项目一代码/AI-infra/models/your-model.gguf";
```

修改后需要重新编译：

```bash
cd build
make -j$(sysctl -n hw.ncpu)
```

## 运行服务器

```bash
# 在 build 目录中运行
./ai_infra_server_mac

# 或者使用绝对路径
/Users/你的用户名/Documents/Code/项目一代码/AI-infra/AI-chats-mac/build/ai_infra_server_mac
```

服务器将在 **8080 端口** 启动，你会看到类似输出：

```
[Model] loaded ok: ../models/your-model.gguf
Server listening on port 8080...
```

## API 测试

### 1. 健康检查

```bash
curl http://localhost:8080/
# 输出: Hello, World!
```

### 2. 用户注册

```bash
curl -X POST http://localhost:8080/register \
  -H "Content-Type: application/json" \
  -d '{"username":"test_user","password":"test_pass"}'
```

### 3. 用户登录

```bash
curl -X POST http://localhost:8080/login \
  -H "Content-Type: application/json" \
  -d '{"username":"test_user","password":"test_pass"}'
```

### 4. AI 推理 (核心功能)

```bash
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{
    "prompt": "What is artificial intelligence?",
    "chat_id": "session_001"
  }'
```

响应示例：

```json
{
  "answer": "Artificial intelligence (AI) refers to the simulation of human intelligence in machines..."
}
```

### 5. 重置会话

```bash
curl -X POST http://localhost:8080/reset \
  -H "Content-Type: application/json" \
  -d '{"chat_id": "session_001"}'
```

## 项目结构

```
AI-chats-mac/
├── CMakeLists.txt          # Mac 版本 CMake 配置
├── README.md               # 本文档
├── include/                # 头文件
│   ├── HttpServer.h        # HTTP 服务器 (使用 kqueue)
│   ├── ThreadPool.h        # 线程池 (已修复 C++20 兼容性)
│   ├── Router.h            # 路由器
│   ├── HttpRequest.h       # HTTP 请求解析
│   ├── HttpResponse.h      # HTTP 响应构造
│   ├── Database.h          # SQLite 数据库封装
│   ├── Logger.h            # 日志工具
│   └── FileUtils.h         # 文件工具
├── src/
│   ├── main.cpp            # 程序入口
│   └── inference/
│       ├── ModelManager.h  # LLM 模型管理器
│       └── ModelManager.cpp
└── build/                  # 编译输出目录
    └── ai_infra_server_mac # 可执行文件
```

## 技术细节

### kqueue vs epoll

**Linux (epoll)**:
```cpp
epollfd = epoll_create1(0);
epoll_ctl(epollfd, EPOLL_CTL_ADD, server_fd, &ev);
epoll_wait(epollfd, events, max_events, -1);
```

**Mac (kqueue)**:
```cpp
kq = kqueue();
EV_SET(&ev, server_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
kevent(kq, &ev, 1, nullptr, 0, nullptr);
kevent(kq, nullptr, 0, events, max_events, nullptr);
```

### Metal GPU 加速

Mac 版本默认启用 Metal GPU 加速，利用 Apple Silicon 的神经引擎和 GPU 加速 LLM 推理。

查看是否启用：
```bash
cd build
cmake .. | grep "Metal GPU acceleration"
# 输出: -- Metal GPU acceleration enabled
```

### 性能优化

1. **Apple Silicon 优化**: 编译时使用 `-mcpu=apple-m1` 针对 M 系列芯片优化
2. **线程池**: 默认 4 个工作线程，可根据需要调整
3. **非阻塞 I/O**: 使用 kqueue 实现高并发连接处理
4. **Metal 加速**: 利用 Apple GPU 进行模型推理

## 故障排除

### 编译错误

**问题**: `sqlite3` not found
```bash
# Mac 系统自带 SQLite3，但可能需要指定路径
cmake -DSQLITE3_INCLUDE_DIR=/usr/include ..
```

**问题**: CMake version too old
```bash
# 升级 CMake
brew upgrade cmake
```

### 运行时错误

**问题**: Model load failed
- 检查模型文件路径是否正确
- 确认模型文件格式为 GGUF
- 确保有足够的内存加载模型

**问题**: Port 8080 already in use
```bash
# 查找占用端口的进程
lsof -i :8080

# 终止进程
kill -9 <PID>

# 或修改 src/main.cpp 第 19 行的端口号
```

## 性能对比

| 架构 | 模型 | 推理速度 | 内存占用 |
|------|------|----------|----------|
| Apple M1 Pro (Metal) | TinyLlama-1.1B-Q4 | ~40 tokens/s | ~1.5GB |
| Apple M1 Pro (CPU) | TinyLlama-1.1B-Q4 | ~15 tokens/s | ~1.5GB |
| Intel Mac (CPU) | TinyLlama-1.1B-Q4 | ~8 tokens/s | ~1.5GB |

*实际性能取决于具体硬件配置和模型大小*

## 与 Linux 版本的区别

| 特性 | Linux 版本 | Mac 版本 |
|------|-----------|---------|
| I/O 多路复用 | epoll | kqueue |
| GPU 加速 | CUDA/CUBLAS | Metal |
| 编译选项 | x86_64/aarch64 | Apple Silicon/Intel |
| C++ 标准库 | std::result_of | std::invoke_result |

## 开发建议

1. **调试模式编译**:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   make -j$(sysctl -n hw.ncpu)
   ```

2. **启用更多日志**:
   修改 `include/Logger.h` 设置日志级别

3. **压力测试**:
   ```bash
   # 使用 Apache Bench
   brew install apache2
   ab -n 1000 -c 10 http://localhost:8080/
   ```

## License

与原项目保持一致。

## 贡献

欢迎提交 Issue 和 Pull Request！

## 联系方式

如有问题，请参考原项目文档或提交 Issue。
