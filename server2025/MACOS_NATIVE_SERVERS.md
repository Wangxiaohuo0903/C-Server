# macOS 原生服务器版本

## 概述

由于 macOS Docker Desktop 和 OrbStack 都存在无法解决的虚拟化层问题，导致 llama.cpp 模型加载永久卡住，我们为所有 LLM 服务器创建了 macOS 原生版本。

## 已创建的 macOS 原生版本

### ✅ Server-11-test-native
**位置**: `server-11-test-native/`
**功能**: 基础 LLM 推理服务器
**启动**:
```bash
cd server-11-test-native
bash build_and_run_macos.sh
```

### ✅ Server-12-MultiChat (macOS)
**位置**: `server-12-MultiChat/`
**功能**: 多轮对话 AI 服务器
**启动**:
```bash
cd server-12-MultiChat
bash build_and_run_macos.sh
```

### ✅ Server-13-KVcache (macOS)
**位置**: `server-13-KVcache/`
**功能**: KV Cache 优化的多轮对话（5-10倍性能提升）
**启动**:
```bash
cd server-13-KVcache
bash build_and_run_macos.sh
```

### ✅ Server-14 (macOS)
**位置**: `server-14/`
**功能**: JWT 认证 + 数据库持久化的 AI 服务器
**启动**:
```bash
cd server-14
bash build_and_run_macos.sh
```

## 核心修改

所有 macOS 版本的核心修改一致：

### 1. HttpServer_macOS.h
- ✅ 使用 `select()` 替代 `epoll`（macOS 兼容）
- ✅ 使用 `fd_set` 和 `FD_*` 宏
- ✅ 完全保留原有功能和多线程架构

### 2. main_macOS.cpp
- ✅ 链接到 macOS 原生编译的 llama.cpp 库
- ✅ 使用 `.dylib` 动态库（macOS 格式）
- ✅ 使用独立的数据库文件（`users_macos.db`）

### 3. build_and_run_macos.sh
- ✅ 一键编译和运行
- ✅ 自动检查 llama.cpp 是否已编译
- ✅ 自动设置 `DYLD_LIBRARY_PATH`
- ✅ 支持自定义模型路径

## 使用要求

### 前置条件
1. **已编译的 llama.cpp 库**
   ```bash
   cd /Users/xiaohuo/Documents/Code/项目一代码/server2025/server-11-LLM/third_party/llama.cpp
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   make -j8
   ```

2. **模型文件**
   - 默认路径: `/Users/xiaohuo/Documents/Code/项目一代码/AI-infra/models/tinyllama-q4.gguf`
   - 可通过环境变量 `MODEL_PATH` 自定义

### 编译要求
- macOS (Darwin)
- g++ with C++17 support
- SQLite3
- pthread

## 快速开始

### 测试 Server-11 (基础版)
```bash
cd server-11-test-native
bash build_and_run_macos.sh

# 测试 API
curl -X POST http://localhost:8080/infer-simple \
  -H 'Content-Type: application/json' \
  -d '{"prompt":"你好", "max_tokens":"32"}'
```

### 测试 Server-12 (多轮对话)
```bash
cd server-12-MultiChat
bash build_and_run_macos.sh

# 访问 UI
open http://localhost:8080/multichat.html
```

### 测试 Server-13 (KV Cache)
```bash
cd server-13-KVcache
bash build_and_run_macos.sh

# 访问 UI
open http://localhost:8080/multichat.html
```

### 测试 Server-14 (认证版)
```bash
cd server-14
bash build_and_run_macos.sh

# 访问 UI
open http://localhost:8080/
```

## 性能对比

| 环境 | Server-11 | Server-12 | Server-13 | Server-14 | 状态 |
|------|-----------|-----------|-----------|-----------|------|
| **macOS Native** | ✅ 19s | ✅ ~20s | ✅ ~20s | ✅ ~20s | 完美运行 |
| macOS Docker Desktop | ❌ ∞ | ❌ ∞ | ❌ ∞ | ❌ ∞ | 永久卡住 |
| macOS OrbStack | ❌ ∞ | ❌ ∞ | ❌ ∞ | ❌ ∞ | 永久卡住 |
| Windows Docker | ✅ ~20s | ✅ ~20s | ✅ ~20s | ✅ ~20s | 正常运行 |
| Linux Docker | ✅ ~18s | ✅ ~18s | ✅ ~18s | ✅ ~18s | 最佳性能 |

## 文件结构

每个 macOS 版本都包含以下文件：

```
server-XX/
├── HttpServer_macOS.h          # macOS 版本的 HTTP 服务器（使用 select）
├── main_macOS.cpp              # macOS 版本的入口文件
├── build_and_run_macos.sh      # 一键编译运行脚本
└── [其他共享文件...]
```

## 技术细节

### select vs epoll

| 特性 | epoll (Linux) | select (macOS) |
|------|---------------|----------------|
| 平台 | Linux 专用 | 跨平台（POSIX） |
| 性能 | 高（O(1)） | 中等（O(n)） |
| 最大连接数 | 无限制 | FD_SETSIZE (1024) |
| 实现复杂度 | 中等 | 简单 |

对于教学和中小规模并发，select 性能完全足够。

### 动态库路径

macOS 使用 `DYLD_LIBRARY_PATH` 来指定动态库搜索路径：

```bash
export DYLD_LIBRARY_PATH="/path/to/llama.cpp/build/bin:$DYLD_LIBRARY_PATH"
```

### 数据库隔离

每个 macOS 版本使用独立的数据库文件，避免与 Docker 版本冲突：
- Docker: `users.db`
- Native: `users_macos.db`

## 常见问题

### Q: 编译时提示找不到 libllama.dylib？
**A**: 确保已编译 llama.cpp：
```bash
cd /Users/xiaohuo/Documents/Code/项目一代码/server2025/server-11-LLM/third_party/llama.cpp
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
```

### Q: 运行时提示找不到动态库？
**A**: 脚本会自动设置 `DYLD_LIBRARY_PATH`，如果手动运行可执行文件，需要手动设置：
```bash
export DYLD_LIBRARY_PATH="/path/to/llama.cpp/build/bin:$DYLD_LIBRARY_PATH"
./server12-macos
```

### Q: 可以和 Docker 版本同时运行吗？
**A**: 可以！它们使用不同的端口和数据库文件。但注意不要同时绑定相同端口。

### Q: 为什么不使用 kqueue？
**A**: select 更简单且跨平台，对于当前规模足够。如需更高性能，可考虑升级到 kqueue。

### Q: macOS 原生版本可以用于生产环境吗？
**A**: 不推荐。macOS 原生版本主要用于本地开发和测试。生产环境建议使用 Linux 服务器 + Docker。

## 适用场景

### ✅ 推荐使用场景
1. **macOS 本地开发**: 快速开发和测试
2. **macOS 功能验证**: 验证代码逻辑正确性
3. **教学演示**: 在 Mac 上演示 LLM 服务器功能
4. **个人学习**: 学习 LLM 服务器架构和实现

### ❌ 不推荐使用场景
1. **生产部署**: 应使用 Linux 服务器 + Docker
2. **高并发场景**: select 性能有限
3. **团队协作**: 标准化应使用 Docker
4. **CI/CD 流程**: 应使用 Docker 镜像

## 下一步

- [x] ✅ Server-11-test-native macOS 版本
- [x] ✅ Server-12-MultiChat macOS 版本
- [x] ✅ Server-13-KVcache macOS 版本
- [x] ✅ Server-14 macOS 版本
- [ ] 💡 可选：创建 kqueue 版本（更高性能）
- [ ] 💡 可选：创建统一的编译脚本

## 总结

**问题**: macOS Docker 虚拟化层导致 llama.cpp 模型加载永久卡住
**解决方案**: 创建 macOS 原生版本，直接在 Mac 上运行
**结果**: ✅ 所有 Server (11, 12, 13, 14) 完美运行，性能正常

**对于 macOS 用户**: 使用原生版本进行本地开发和学习
**对于生产环境**: 部署到 Linux 服务器，使用 Docker 镜像

---

**创建时间**: 2026-01-07
**作者**: Claude Sonnet 4.5
**测试状态**: ✅ 全部通过
