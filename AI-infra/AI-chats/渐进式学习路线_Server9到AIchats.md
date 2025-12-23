# 渐进式学习路线：从 Server-9 到 AI-chats

## 📋 设计理念

Server-1 到 Server-9 展示了如何逐步构建一个完整的HTTP服务器，每一步都只添加一个核心功能。但是从 Server-9 到 AI-chats 跳跃太大，中间缺少了渐进式的学习步骤。

本文档设计了 **6个中间版本**（Server-10 到 Server-15），帮助你循序渐进地掌握LLM推理服务开发。

---

## 🎯 完整学习路线图

```
Server-1    Hello World                   (HTTP基础)
    ↓
Server-2    HTTP协议解析                  (请求响应)
    ↓
Server-3    日志系统                      (Logger)
    ↓
Server-4    数据库集成                    (SQLite)
    ↓
Server-5    epoll多路复用                 (高性能I/O)
    ↓
Server-6    线程池                        (并发处理)
    ↓
Server-7    路由系统                      (Router)
    ↓
Server-8    前端UI                        (用户界面)
    ↓
Server-9    Nginx反向代理                 (生产部署)
    ↓
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    ↓
Server-10   JSON解析 + RESTful API       ⭐ 新增
    ↓
Server-11   llama.cpp集成 + 单轮推理     ⭐ 新增
    ↓
Server-12   会话管理 + 多轮对话          ⭐ 新增
    ↓
Server-13   CMake构建 + 模块化重构       ⭐ 新增
    ↓
Server-14   Docker基础部署               ⭐ 新增
    ↓
Server-15   Docker优化 + 生产配置        ⭐ 新增
    ↓
AI-chats    完整AI推理服务               (最终版本)
```

---

## 📚 详细步骤规划

### Server-10: JSON解析 + RESTful API

**学习目标**:
- 掌握JSON数据格式
- 实现parseJson()方法
- 设计RESTful风格的API接口

**新增功能**:
1. ✅ 手写简单的JSON解析器
2. ✅ 支持POST请求的JSON body解析
3. ✅ 重构现有接口为RESTful风格

**核心代码** (~150行):

```cpp
// HttpRequest.h 新增方法
class HttpRequest {
public:
    // 解析JSON格式的body
    std::map<std::string, std::string> parseJson() const {
        std::map<std::string, std::string> result;
        std::string json = body_;

        // 简单JSON解析（支持{"key":"value", "key2":"value2"}格式）
        size_t pos = 0;
        while ((pos = json.find("\"", pos)) != std::string::npos) {
            size_t key_start = pos + 1;
            size_t key_end = json.find("\"", key_start);
            std::string key = json.substr(key_start, key_end - key_start);

            pos = json.find(":", key_end);
            pos = json.find("\"", pos);
            size_t val_start = pos + 1;
            size_t val_end = json.find("\"", val_start);
            std::string value = json.substr(val_start, val_end - val_start);

            result[key] = value;
            pos = val_end + 1;
        }
        return result;
    }
};
```

**新增API接口**:
```
POST /api/users/register    # RESTful风格
POST /api/users/login
GET  /api/users/:id          # 获取用户信息
```

**测试示例**:
```bash
# 使用JSON格式注册
curl -X POST http://localhost:8080/api/users/register \
  -H "Content-Type: application/json" \
  -d '{"username":"alice","password":"pass123"}'
```

**文件清单**:
```
server-10/
├── HttpRequest.h        # 新增parseJson()方法
├── Router.h             # RESTful API路由
├── main.cpp
└── README.md            # JSON解析教程
```

**代码增量**: +150行
**学习时间**: 2-3小时
**难度**: ⭐⭐☆☆☆

---

### Server-11: llama.cpp集成 + 单轮推理

**学习目标**:
- 理解llama.cpp库的基本使用
- 加载GGUF格式模型
- 实现简单的文本生成

**新增功能**:
1. ✅ 下载和编译llama.cpp
2. ✅ 加载量化模型（如TinyLlama）
3. ✅ 实现单轮推理（无历史记录）
4. ✅ 添加 `/infer-simple` 接口

**核心代码** (~300行):

```cpp
// SimpleInference.h - 简单推理类
#pragma once
#include "llama.h"
#include <string>

class SimpleInference {
public:
    // 加载模型
    bool loadModel(const std::string& model_path) {
        llama_model_params mp = llama_model_default_params();
        model_ = llama_load_model_from_file(model_path.c_str(), mp);
        return model_ != nullptr;
    }

    // 单轮推理（无历史）
    std::string generate(const std::string& prompt, int max_tokens = 64) {
        // 1. 创建context
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = 2048;
        cp.n_threads = 4;
        llama_context* ctx = llama_new_context_with_model(model_, cp);

        // 2. Tokenize
        std::vector<llama_token> tokens = tokenize(prompt);

        // 3. 处理prompt
        llama_decode(ctx, make_batch(tokens));

        // 4. 生成tokens
        std::string output;
        for (int i = 0; i < max_tokens; ++i) {
            llama_token next = sample_token(ctx);
            if (next == llama_token_eos(model_)) break;
            output += token_to_string(next);
        }

        llama_free(ctx);
        return output;
    }

private:
    llama_model* model_ = nullptr;
    // ... 辅助方法实现
};
```

**新增API**:
```cpp
// Router.h
addRoute("POST", "/infer-simple", [&inference](const HttpRequest& r) {
    auto js = r.parseJson();
    std::string prompt = js["prompt"];
    std::string response = inference.generate(prompt, 64);

    HttpResponse resp(200);
    resp.setHeader("Content-Type", "application/json");
    resp.setBody("{\"response\":\"" + response + "\"}");
    return resp;
});
```

**测试示例**:
```bash
# 单轮推理测试
curl -X POST http://localhost:8080/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"Hello, who are you?"}'

# 响应:
# {"response":"I am an AI assistant..."}
```

**文件清单**:
```
server-11/
├── SimpleInference.h    # ⭐ 新增：简单推理类
├── SimpleInference.cpp
├── Router.h             # 添加/infer-simple接口
├── main.cpp             # 加载模型
├── CMakeLists.txt       # ⭐ 新增：构建配置
├── download_model.sh    # ⭐ 新增：下载模型脚本
└── README.md            # llama.cpp使用教程
```

**依赖安装**:
```bash
# 克隆llama.cpp
cd ../third_party
git clone https://github.com/ggerganov/llama.cpp.git

# 编译llama.cpp
cd llama.cpp
make -j$(nproc)

# 下载模型
wget https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf
mv tinyllama-*.gguf ../../models/
```

**代码增量**: +300行
**学习时间**: 4-6小时
**难度**: ⭐⭐⭐⭐☆

**关键概念**:
- GGUF模型格式
- Tokenization
- Context和Batch
- 贪婪采样
- EOS token

---

### Server-12: 会话管理 + 多轮对话

**学习目标**:
- 理解多轮对话的上下文维护
- 实现ChatSession会话类
- 掌握ChatML格式

**新增功能**:
1. ✅ ChatSession类（维护对话历史）
2. ✅ makePrompt()构造ChatML格式
3. ✅ 自动历史裁剪（prune）
4. ✅ 会话隔离（每个chat_id独立）

**核心代码** (~200行):

```cpp
// ChatSession.h
#pragma once
#include <vector>
#include <string>

struct Message {
    std::string role;     // "user" | "assistant"
    std::string content;
};

class ChatSession {
public:
    void addMessage(const std::string& role, const std::string& content) {
        history_.push_back({role, content});
        pruneHistory();  // 自动裁剪
    }

    std::string makePrompt() const {
        std::string prompt;
        for (const auto& msg : history_) {
            prompt += "<|" + msg.role + "|>\n";
            prompt += msg.content + "\n";
        }
        prompt += "<|assistant|>\n";  // 提示模型生成
        return prompt;
    }

    void clear() { history_.clear(); }

private:
    std::vector<Message> history_;

    void pruneHistory() {
        // 保留最近4轮对话（8条消息）
        const int MAX_MESSAGES = 8;
        if (history_.size() > MAX_MESSAGES) {
            history_.erase(history_.begin(),
                          history_.begin() + (history_.size() - MAX_MESSAGES));
        }
    }
};
```

**SessionManager** (~100行):
```cpp
// SessionManager.h
#pragma once
#include "ChatSession.h"
#include <unordered_map>
#include <mutex>

class SessionManager {
public:
    ChatSession& getSession(const std::string& chat_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return sessions_[chat_id];  // 自动创建
    }

    void deleteSession(const std::string& chat_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.erase(chat_id);
    }

private:
    std::unordered_map<std::string, ChatSession> sessions_;
    std::mutex mutex_;
};
```

**更新推理接口**:
```cpp
// Router.h
SessionManager session_mgr;

addRoute("POST", "/infer", [&](const HttpRequest& r) {
    auto js = r.parseJson();
    std::string chat_id = js["chat_id"];
    std::string user_msg = js["prompt"];

    // 1. 添加用户消息到会话
    auto& session = session_mgr.getSession(chat_id);
    session.addMessage("user", user_msg);

    // 2. 构造完整prompt（包含历史）
    std::string prompt = session.makePrompt();

    // 3. 生成回复
    std::string response = inference.generate(prompt, 64);

    // 4. 保存AI回复到会话
    session.addMessage("assistant", response);

    // 5. 返回结果
    HttpResponse resp(200);
    resp.setHeader("Content-Type", "application/json");
    resp.setBody("{\"response\":\"" + response + "\"}");
    return resp;
});
```

**多轮对话测试**:
```bash
# 第一轮
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{"chat_id":"session001","prompt":"My name is Alice"}'
# 响应: {"response":"Nice to meet you, Alice!"}

# 第二轮（测试记忆）
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{"chat_id":"session001","prompt":"What is my name?"}'
# 响应: {"response":"Your name is Alice."}  ✅ 记住了！
```

**文件清单**:
```
server-12/
├── ChatSession.h        # ⭐ 新增：会话管理
├── SessionManager.h     # ⭐ 新增：全局会话管理器
├── SimpleInference.h    # 从server-11继承
├── Router.h             # 更新/infer接口
├── main.cpp
└── README.md            # 多轮对话教程
```

**代码增量**: +200行
**学习时间**: 3-4小时
**难度**: ⭐⭐⭐☆☆

**关键概念**:
- ChatML格式
- 会话隔离
- 上下文窗口
- 历史裁剪策略

---

### Server-13: CMake构建 + 模块化重构

**学习目标**:
- 掌握CMake构建系统
- 代码模块化组织
- 子目录管理

**新增功能**:
1. ✅ 标准CMake项目结构
2. ✅ 模块化目录组织（include/ + src/）
3. ✅ 子项目管理（llama.cpp）
4. ✅ 编译选项配置

**项目结构重构**:
```
server-13/
├── CMakeLists.txt           # ⭐ 主构建配置
├── include/                 # ⭐ 头文件目录
│   ├── HttpServer.h
│   ├── HttpRequest.h
│   ├── HttpResponse.h
│   ├── Router.h
│   ├── Database.h
│   ├── ThreadPool.h
│   └── Logger.h
├── src/                     # ⭐ 源文件目录
│   ├── main.cpp
│   └── inference/           # ⭐ 推理模块
│       ├── SimpleInference.h
│       ├── SimpleInference.cpp
│       ├── ChatSession.h
│       └── SessionManager.h
├── third_party/             # ⭐ 第三方库
│   └── llama.cpp/          (git submodule)
├── models/                  # 模型文件
│   └── tinyllama-q4.gguf
├── build/                   # 构建目录
└── README.md
```

**CMakeLists.txt** (~80行):
```cmake
cmake_minimum_required(VERSION 3.22)
project(ai_infra_server VERSION 1.0 LANGUAGES C CXX)

# C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ========== llama.cpp子项目 ==========
message(STATUS "Adding llama.cpp...")
add_subdirectory(third_party/llama.cpp)

# ========== 包含目录 ==========
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/include
    ${CMAKE_SOURCE_DIR}/third_party/llama.cpp/ggml/include
)

# ========== 源文件 ==========
set(SOURCES
    src/main.cpp
    src/inference/SimpleInference.cpp
)

# ========== 可执行文件 ==========
add_executable(ai_infra_server ${SOURCES})

# ========== 链接库 ==========
target_link_libraries(ai_infra_server
    PRIVATE
    llama              # llama.cpp主库
    sqlite3            # SQLite数据库
    pthread            # 线程库
)

# ========== 安装规则 ==========
install(TARGETS ai_infra_server DESTINATION bin)

# ========== 编译信息 ==========
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
```

**构建脚本** (`build.sh`):
```bash
#!/bin/bash
set -e

echo "=== Building AI-Infra Server ==="

# 1. 创建构建目录
mkdir -p build
cd build

# 2. 配置CMake
echo "Configuring CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DGGML_NATIVE=OFF \
    -DGGML_CPU_ALL_VARIANTS=OFF

# 3. 编译
echo "Building..."
make -j$(nproc)

# 4. 显示结果
echo "Build complete!"
ls -lh ai_infra_server

echo "Run with: ./build/ai_infra_server"
```

**使用方式**:
```bash
# 方式1: 使用构建脚本
chmod +x build.sh
./build.sh

# 方式2: 手动构建
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 运行
./build/ai_infra_server
```

**文件清单**:
```
server-13/
├── CMakeLists.txt       # ⭐ 新增：CMake配置
├── build.sh             # ⭐ 新增：构建脚本
├── include/             # ⭐ 重构：头文件目录
├── src/                 # ⭐ 重构：源文件目录
├── third_party/         # ⭐ 新增：第三方库
└── README.md            # CMake教程
```

**代码增量**: +80行配置，代码重组织
**学习时间**: 2-3小时
**难度**: ⭐⭐⭐☆☆

**关键概念**:
- CMake基础语法
- target vs project
- include_directories
- add_subdirectory
- 编译选项

---

### Server-14: Docker基础部署

**学习目标**:
- 掌握Docker基础
- 编写Dockerfile
- 容器化应用

**新增功能**:
1. ✅ 基础Dockerfile
2. ✅ Docker镜像构建
3. ✅ 容器运行和测试

**Dockerfile** (~30行):
```dockerfile
# 使用Ubuntu 22.04作为基础镜像
FROM ubuntu:22.04

# 避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libsqlite3-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . /app/

# 克隆llama.cpp
RUN mkdir -p third_party && \
    cd third_party && \
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git

# 编译
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# 暴露端口
EXPOSE 8080

# 运行服务器
CMD ["./build/ai_infra_server"]
```

**构建和运行**:
```bash
# 1. 构建镜像
docker build -t ai-infra-server:v1 .

# 2. 运行容器
docker run -d \
    --name ai-server \
    -p 8080:8080 \
    -v $(pwd)/models:/app/models:ro \
    ai-infra-server:v1

# 3. 查看日志
docker logs -f ai-server

# 4. 测试
curl -X POST http://localhost:8080/infer \
  -H "Content-Type: application/json" \
  -d '{"chat_id":"test","prompt":"Hello"}'

# 5. 停止容器
docker stop ai-server
docker rm ai-server
```

**辅助脚本** (`docker-build.sh`):
```bash
#!/bin/bash
set -e

IMAGE_NAME="ai-infra-server"
TAG="v1"

echo "Building Docker image: ${IMAGE_NAME}:${TAG}"
docker build -t ${IMAGE_NAME}:${TAG} .

echo "Image built successfully!"
docker images | grep ${IMAGE_NAME}
```

**文件清单**:
```
server-14/
├── Dockerfile           # ⭐ 新增：Docker配置
├── docker-build.sh      # ⭐ 新增：构建脚本
├── docker-run.sh        # ⭐ 新增：运行脚本
├── .dockerignore        # ⭐ 新增：忽略文件
├── CMakeLists.txt
├── src/
├── include/
└── README.md            # Docker教程
```

**代码增量**: +50行配置
**学习时间**: 2-3小时
**难度**: ⭐⭐☆☆☆

**关键概念**:
- Docker镜像 vs 容器
- Dockerfile指令（FROM, RUN, COPY, CMD）
- 端口映射
- 卷挂载

---

### Server-15: Docker优化 + 生产配置

**学习目标**:
- 多阶段构建优化
- docker-compose使用
- 环境变量配置
- 生产级部署

**新增功能**:
1. ✅ 多阶段构建（减小镜像体积）
2. ✅ docker-compose编排
3. ✅ 环境变量支持
4. ✅ 健康检查
5. ✅ 镜像源优化

**优化的Dockerfile** (~60行):
```dockerfile
# ========== 阶段1: 构建阶段 ==========
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# ⭐ 使用阿里云镜像源（加速下载）
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential cmake git libsqlite3-dev wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# 克隆llama.cpp
RUN mkdir -p third_party && \
    cd third_party && \
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git

# 复制源代码
COPY . /build/app/

# 编译
WORKDIR /build/app/build
RUN cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)

# ========== 阶段2: 运行阶段 ==========
FROM ubuntu:22.04

# 同样使用阿里云镜像
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

# ⭐ 只安装运行时依赖（大幅减小镜像）
RUN apt-get update && apt-get install -y \
    libsqlite3-0 \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# ⭐ 从构建阶段复制编译产物
COPY --from=builder /build/app/build/ai_infra_server /app/
COPY --from=builder /build/app/build/bin/*.so* /usr/lib/

# 更新动态链接库
RUN ldconfig

# 创建目录
RUN mkdir -p /app/models

EXPOSE 8080

# ⭐ 环境变量支持
ENV MODEL_PATH=/app/models/tinyllama-q4.gguf
ENV LOG_LEVEL=INFO

CMD ["./ai_infra_server"]
```

**docker-compose.yml** (~40行):
```yaml
version: '3.8'

services:
  ai-infra-server:
    build:
      context: .
      dockerfile: Dockerfile
    container_name: ai-server
    ports:
      - "8080:8080"
    volumes:
      # 挂载模型目录（只读）
      - ../models:/app/models:ro
      # 挂载数据库（持久化）
      - ./users.db:/app/users.db
    environment:
      - MODEL_PATH=/app/models/tinyllama-q4.gguf
      - LOG_LEVEL=INFO
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "wget", "--no-verbose", "--tries=1", "--spider", "http://localhost:8080/"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 10s
    networks:
      - ai-network

networks:
  ai-network:
    driver: bridge
```

**使用docker-compose**:
```bash
# 1. 构建并启动
docker-compose up -d

# 2. 查看日志
docker-compose logs -f

# 3. 停止服务
docker-compose down

# 4. 重新构建
docker-compose build --no-cache
docker-compose up -d
```

**环境变量配置** (`.env`):
```bash
# 模型配置
MODEL_PATH=/app/models/tinyllama-q4.gguf

# 服务配置
PORT=8080
LOG_LEVEL=INFO

# 数据库
DB_PATH=/app/users.db
```

**生产级脚本** (`deploy.sh`):
```bash
#!/bin/bash
set -e

echo "=== AI-Infra Server Deployment ==="

# 检查模型文件
if [ ! -f "../models/tinyllama-q4.gguf" ]; then
    echo "Error: Model file not found!"
    echo "Please download model to: ../models/tinyllama-q4.gguf"
    exit 1
fi

# 停止旧容器
echo "Stopping old containers..."
docker-compose down || true

# 构建新镜像
echo "Building new image..."
docker-compose build

# 启动服务
echo "Starting service..."
docker-compose up -d

# 等待服务启动
echo "Waiting for service to start..."
sleep 10

# 健康检查
echo "Health check..."
curl -f http://localhost:8080/ || {
    echo "Health check failed!"
    docker-compose logs
    exit 1
}

echo "Deployment successful!"
docker-compose ps
```

**镜像优化对比**:
```
Server-14（单阶段构建）:
- 镜像大小: ~2.5GB
- 包含构建工具: ✅
- 适合开发: ✅

Server-15（多阶段构建）:
- 镜像大小: ~800MB  (-68%)
- 包含构建工具: ❌
- 适合生产: ✅
```

**文件清单**:
```
server-15/
├── Dockerfile           # ⭐ 优化：多阶段构建
├── docker-compose.yml   # ⭐ 新增：容器编排
├── .env                 # ⭐ 新增：环境变量
├── .dockerignore        # ⭐ 优化：忽略更多文件
├── deploy.sh            # ⭐ 新增：部署脚本
├── CMakeLists.txt
├── src/
├── include/
└── README.md            # 生产部署教程
```

**代码增量**: +120行配置
**学习时间**: 3-4小时
**难度**: ⭐⭐⭐☆☆

**关键概念**:
- 多阶段构建
- docker-compose
- 环境变量
- 健康检查
- 卷持久化
- 生产部署

---

## 📊 完整对比表格

| 版本 | 核心功能 | 新增代码 | 难度 | 学习时间 |
|------|---------|---------|------|---------|
| Server-9 | Nginx反向代理 | - | - | - |
| **Server-10** | JSON解析 + RESTful | +150行 | ⭐⭐☆☆☆ | 2-3h |
| **Server-11** | llama.cpp + 单轮推理 | +300行 | ⭐⭐⭐⭐☆ | 4-6h |
| **Server-12** | 会话管理 + 多轮对话 | +200行 | ⭐⭐⭐☆☆ | 3-4h |
| **Server-13** | CMake + 模块化 | +80行 | ⭐⭐⭐☆☆ | 2-3h |
| **Server-14** | Docker基础 | +50行 | ⭐⭐☆☆☆ | 2-3h |
| **Server-15** | Docker优化 | +120行 | ⭐⭐⭐☆☆ | 3-4h |
| **AI-chats** | 完整版本 | +300行 | ⭐⭐⭐⭐☆ | 2-3h |
| **总计** | | **+1200行** | | **18-26h** |

---

## 🎯 学习路径建议

### 路径A: 完整学习（推荐）
按顺序学习Server-10到Server-15，每个版本都动手实践。

**适合**: 初学者、希望深入理解每个概念

**时间**: 3-4周（每天1-2小时）

---

### 路径B: 快速通道
重点学习Server-11（推理）、Server-12（多轮对话）、Server-15（部署）

**适合**: 有基础的开发者、时间有限

**时间**: 1-2周

---

### 路径C: 跳跃式
直接学习AI-chats完整版本，遇到不懂的概念再回头看对应章节。

**适合**: 有经验的开发者、急于上手

**时间**: 3-5天

---

## 📂 实现计划

### 第一阶段：创建基础版本（1周）
- ✅ Server-10: JSON解析
- ✅ Server-11: llama.cpp集成
- ✅ Server-12: 多轮对话

### 第二阶段：完善部署（3天）
- ✅ Server-13: CMake构建
- ✅ Server-14: Docker基础
- ✅ Server-15: 生产配置

### 第三阶段：文档和测试（2天）
- ✅ 每个版本的README
- ✅ 测试脚本
- ✅ 示例代码

---

## 💡 每个版本的核心价值

| 版本 | 核心价值 | 实际应用 |
|------|---------|---------|
| Server-10 | 数据交换格式 | 所有现代Web API |
| Server-11 | AI模型调用 | ChatGPT、Claude等 |
| Server-12 | 对话记忆 | 聊天机器人 |
| Server-13 | 项目管理 | 大型C++项目 |
| Server-14 | 应用容器化 | 云原生部署 |
| Server-15 | 生产优化 | 企业级服务 |

---

## 🔗 代码复用关系

```
Server-9 ────────────────────────────────┐
    ↓                                     │
Server-10 (JSON)                         │
    ↓                                     │ 复用所有
Server-11 (llama.cpp) ─────┐             │ 基础组件
    ↓                      │             │
Server-12 (+Session)       │             │
    ↓                      │             │
Server-13 (重组织) ←───────┘             │
    ↓                                     │
Server-14 (Docker)                       │
    ↓                                     │
Server-15 (优化)                         │
    ↓                                     │
AI-chats (完整版) ←──────────────────────┘
```

**每个版本都是在前一个版本基础上增量开发！**

---

## 📚 配套资源

### 每个版本包含：
1. ✅ 完整源代码
2. ✅ README教程
3. ✅ 构建脚本
4. ✅ 测试示例
5. ✅ 常见问题

### 额外资源：
- 📖 llama.cpp使用手册
- 📖 CMake入门指南
- 📖 Docker最佳实践
- 📖 ChatML格式说明

---

## 🎓 学习检查清单

### Server-10 ✅
- [ ] 理解JSON格式
- [ ] 实现parseJson()方法
- [ ] 设计RESTful API
- [ ] 测试JSON请求

### Server-11 ✅
- [ ] 编译llama.cpp
- [ ] 加载GGUF模型
- [ ] 理解tokenization
- [ ] 实现贪婪采样
- [ ] 测试单轮推理

### Server-12 ✅
- [ ] 理解ChatML格式
- [ ] 实现ChatSession
- [ ] 维护对话历史
- [ ] 实现历史裁剪
- [ ] 测试多轮对话

### Server-13 ✅
- [ ] 编写CMakeLists.txt
- [ ] 组织项目结构
- [ ] 管理依赖库
- [ ] 使用CMake构建

### Server-14 ✅
- [ ] 编写Dockerfile
- [ ] 构建Docker镜像
- [ ] 运行容器
- [ ] 卷挂载

### Server-15 ✅
- [ ] 多阶段构建
- [ ] docker-compose编排
- [ ] 环境变量配置
- [ ] 健康检查
- [ ] 生产部署

---

## 🚀 开始行动

### 推荐第一步：
1. 从Server-10开始，实现JSON解析
2. 测试POST请求
3. 理解RESTful设计

### 常见问题：
**Q: 是否需要全部学完？**
A: 不需要。根据你的目标选择路径A/B/C。

**Q: 可以跳过某些版本吗？**
A: Server-11和Server-12是核心，不建议跳过。其他可以根据需要选择。

**Q: 遇到问题怎么办？**
A: 每个版本的README都有详细说明和常见问题解答。

---

**开始你的LLM推理服务开发之旅吧！** 🎉

从Server-10的第一行JSON解析代码，到AI-chats的完整生产系统，每一步都是实实在在的技能提升。

**文档维护**: 如有更新，请同步修改
**最后更新**: 2025-12-10
