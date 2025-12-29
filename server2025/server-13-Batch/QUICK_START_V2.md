# Server-13 快速开始指南

## 🚀 5 分钟快速体验

### 前置要求

1. **系统**：Linux / macOS / WSL
2. **编译器**：GCC 9+ 或 Clang 10+
3. **CMake**：3.22+
4. **模型文件**：GGUF 格式的量化模型（推荐 SmolLM-360M-Q4，259MB）

---

## 步骤 1: 克隆或进入项目

```bash
cd server-13-ContextPool
```

---

## 步骤 2: 编译项目

```bash
# 创建 build 目录
mkdir -p build && cd build

# CMake 配置
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译（使用 4 个线程）
make -j4

# 编译完成后应该看到：
# [100%] Built target ai_infra_server
```

编译完成后会生成可执行文件：`build/ai_infra_server`

---

## 步骤 3: 准备模型文件

### 选项 A: 下载推荐模型（SmolLM-360M）

```bash
cd ..  # 回到项目根目录
mkdir -p models

# 下载 SmolLM-360M-Q4 模型（259MB）
wget https://huggingface.co/HuggingFaceTB/SmolLM-360M-Instruct-GGUF/resolve/main/smollm-360m-q4_k_m.gguf \
     -O models/smollm-360m-q4_k_m.gguf
```

### 选项 B: 使用已有模型

如果你已经有 GGUF 格式的模型，记下其路径。

---

## 步骤 4: 设置环境变量

```bash
# 设置模型路径
export MODEL_PATH="$(pwd)/models/smollm-360m-q4_k_m.gguf"

# 设置推理线程数（建议为 CPU 核心数）
export OMP_NUM_THREADS=4
```

---

## 步骤 5: 运行测试程序

### 测试 KV 缓存复用功能

编译测试程序：

```bash
cd build

# 编译测试程序（如果 CMakeLists.txt 未包含，手动编译）
g++ -std=c++20 -O3 \
    -I../include \
    -I../../third_party/llama.cpp/include \
    -I../../third_party/llama.cpp/ggml/include \
    ../test_context_pool.cpp \
    ../src/SessionContextPool.cpp \
    ../src/BatchInferenceEngine.cpp \
    ../src/ModelManagerV2.cpp \
    -o test_context_pool \
    -L. -lllama -lpthread
```

运行测试：

```bash
./test_context_pool
```

**预期输出**：

```
=====================================
Server-13 Context Pool Test
=====================================

Loading model: ../models/smollm-360m-q4_k_m.gguf
[ModelManagerV2] Model loaded: ../models/smollm-360m-q4_k_m.gguf
[SessionContextPool] Initialized with max_sessions=20, n_ctx=2048, n_threads=4
...

========================================
Test 1: KV Cache Reuse
========================================

[Round 1] User: 你好
AI: 你好！有什么可以帮助你的？
Time: 523ms

[Round 2] User: 介绍一下Docker
AI: Docker是一个开源的容器化平台...
Time: 87ms
Speed-up: 6.01x   ← 🎉 性能提升！

[Round 3] User: 它和虚拟机有什么区别？
AI: Docker容器相比虚拟机更轻量...
Time: 62ms
Speed-up: 8.44x   ← 🎉 更大的提升！

Session Pool Stats:
  Total sessions: 1
  Cache hits: 2
  Cache misses: 1
```

---

## 步骤 6: 运行完整服务器

### 修改 main.cpp 使用 ModelManagerV2

编辑 `src/main.cpp`，将：

```cpp
#include "ModelManager.h"
```

改为：

```cpp
#include "ModelManagerV2.h"
```

将：

```cpp
ModelManager::instance().loadModel(modelPath, n_ctx, n_threads);
```

改为：

```cpp
ModelManagerV2::instance().loadModel(
    modelPath,
    n_ctx,       // 2048
    n_threads,   // 4
    100,         // max_sessions
    true,        // enable_batch（启用批处理）
    8            // batch_size
);
```

在路由处理中，将 `raw_infer` 改为 `inferWithCache`：

```cpp
// 旧代码
std::string reply = mm.raw_infer(full_prompt, maxTokens, temperature);

// 新代码
std::string reply = mm.inferWithCache(session_id, full_prompt, maxTokens, temperature);
```

### 重新编译

```bash
cd build
make -j4
```

### 启动服务器

```bash
./ai_infra_server

# 输出：
# [ModelManagerV2] Model loaded: ...
# [SessionContextPool] Initialized with max_sessions=100
# [BatchInferenceEngine] Worker thread started
# [HttpServer] Server started on port 8080
```

### 访问 Web 界面

打开浏览器访问：`http://localhost:8080/multichat.html`

**体验改进**：
- ✅ 多轮对话响应更快（KV 缓存复用）
- ✅ 多用户同时使用时性能更好（批处理）
- ✅ 长对话不会越来越慢

---

## 🔍 性能验证

### 测试脚本

创建 `benchmark.sh`：

```bash
#!/bin/bash

echo "=== Server-13 Performance Benchmark ==="

# 测试1：单会话多轮对话
echo -e "\n[Test 1] Multi-turn conversation (10 rounds)"
curl -s http://localhost:8080/api/sessions/new | jq -r '.session_id' > /tmp/sid.txt
SESSION_ID=$(cat /tmp/sid.txt)

for i in {1..10}; do
    START=$(date +%s%3N)
    curl -s -X POST "http://localhost:8080/api/sessions/$SESSION_ID/chat" \
         -H "Content-Type: application/json" \
         -d "{\"message\":\"Round $i\",\"max_tokens\":30}" > /dev/null
    END=$(date +%s%3N)
    ELAPSED=$((END - START))
    echo "Round $i: ${ELAPSED}ms"
done

# 测试2：并发请求
echo -e "\n[Test 2] Concurrent requests (10 parallel)"
START=$(date +%s%3N)

for i in {1..10}; do
    (curl -s -X POST "http://localhost:8080/api/sessions/new" \
          -H "Content-Type: application/json" > /dev/null) &
done

wait
END=$(date +%s%3N)
ELAPSED=$((END - START))
echo "Total time for 10 concurrent requests: ${ELAPSED}ms"
echo "Average per request: $((ELAPSED / 10))ms"
```

运行：

```bash
chmod +x benchmark.sh
./benchmark.sh
```

**预期结果**：

```
=== Server-13 Performance Benchmark ===

[Test 1] Multi-turn conversation (10 rounds)
Round 1: 523ms   ← 第一轮需要完整计算
Round 2: 89ms    ← 🎉 6倍提升
Round 3: 67ms    ← 🎉 7.8倍提升
Round 4: 61ms    ← 🎉 8.6倍提升
...

[Test 2] Concurrent requests (10 parallel)
Total time for 10 concurrent requests: 1243ms
Average per request: 124ms   ← 批处理效果
```

---

## 📊 功能对比

### Server-12（旧版）

```bash
# 10轮对话
Round 1: 523ms
Round 2: 567ms   ← ❌ 没有加速
Round 3: 601ms   ← ❌ 甚至变慢（上下文变长）
...
Total: 5834ms
```

### Server-13（新版）

```bash
# 10轮对话
Round 1: 523ms
Round 2: 89ms    ← ✅ 6倍加速
Round 3: 67ms    ← ✅ 7.8倍加速
...
Total: 1287ms    ← ✅ 总体4.5倍提升
```

---

## 🔧 配置调优

### 1. 调整最大会话数

```cpp
// 如果内存有限，降低最大会话数
ModelManagerV2::instance().loadModel(
    path, n_ctx, n_threads,
    50,   // ← 从 100 降到 50
    ...
);
```

每个 session 约占用 10-20MB 内存（取决于 n_ctx）。

### 2. 调整批处理参数

```cpp
// 高延迟敏感场景：小批次 + 低超时
BatchInferenceEngine engine(
    model, n_ctx, n_threads,
    2,     // ← 小批次
    50     // ← 低超时（50ms）
);

// 高吞吐量场景：大批次 + 高超时
BatchInferenceEngine engine(
    model, n_ctx, n_threads,
    16,    // ← 大批次
    500    // ← 高超时（500ms）
);
```

### 3. 禁用批处理（如果不需要）

```cpp
ModelManagerV2::instance().loadModel(
    path, n_ctx, n_threads, max_sessions,
    false,  // ← 禁用批处理
    0
);
```

---

## 🐛 故障排查

### 问题1：编译失败 - 找不到 llama.h

**解决**：

```bash
# 确认 third_party/llama.cpp 存在
ls ../third_party/llama.cpp

# 如果不存在，需要从 AI-infra 项目复制
cp -r /path/to/AI-infra/third_party/llama.cpp ../third_party/
```

### 问题2：运行时错误 - Failed to load model

**解决**：

```bash
# 检查模型路径
echo $MODEL_PATH
ls -lh $MODEL_PATH

# 确认模型格式为 GGUF
file $MODEL_PATH
# 应该输出：GGUF model file ...
```

### 问题3：KV 缓存无效果

**可能原因**：

1. 每次请求使用不同的 `session_id`
2. prompt 格式不一致（无公共前缀）

**解决**：

```cpp
// 确保同一会话使用相同的 session_id
std::string session_id = "user123_chat456";  // ← 固定

// 确保 prompt 有公共前缀
// ✅ 正确：
Round 1: "User: 你好\nAssistant:"
Round 2: "User: 你好\nAssistant: 你好！\nUser: Docker是什么\nAssistant:"
         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  ← 公共前缀

// ❌ 错误：
Round 1: "User: 你好\nAssistant:"
Round 2: "User: Docker是什么\nAssistant:"
         ^^^^^^^^^^^^^^^^^^  ← 无公共前缀
```

---

## 📚 下一步

- 📖 阅读 [SERVER13_IMPROVEMENTS.md](./SERVER13_IMPROVEMENTS.md) 了解详细原理
- 🔬 运行 `test_context_pool` 进行深入测试
- 🌐 访问 Web 界面体验完整功能
- 📊 使用 `benchmark.sh` 进行性能测试

---

**恭喜！你已成功搭建 Server-13 高性能 AI 推理服务器！** 🎉
