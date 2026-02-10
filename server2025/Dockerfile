# 统一 Dockerfile - 支持所有 Server 版本
# 可以交互式选择运行任意 server

FROM ubuntu:22.04

# 设置非交互模式
ENV DEBIAN_FRONTEND=noninteractive

# 安装所有依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    libsqlite3-dev \
    libmongoc-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# 创建工作目录
WORKDIR /app

# 复制所有 server 源代码
WORKDIR /app
COPY server-1-hellowolrd/ /app/server-1/
COPY server-2-http/ /app/server-2/
COPY server-3-Logger/ /app/server-3/
COPY server-4-database/ /app/server-4/
COPY server-5-epoll/ /app/server-5/
COPY server-6-threadpool/ /app/server-6/
COPY server-7-Router/ /app/server-7/
COPY server-8-UI/ /app/server-8/
COPY server-9-Nginx/ /app/server-9/
COPY server-10-JSON/ /app/server-10/
COPY server-10-1-MongoDB/ /app/server-10-1/
COPY server-11-1-SSL/ /app/server-11-1/
COPY server-11-LLM/ /app/server-11/
COPY server-12-MultiChat/ /app/server-12/
COPY server-13-Batch/ /app/server-13/
COPY server-13-KVcache/ /app/server-13-kvcache/
COPY server-14/ /app/server-14/
# Note: server-13-ContextPool doesn't exist as a separate directory

# 删除多余的 llama.cpp 源代码（节省空间 ~600MB-1GB）
RUN echo "Removing duplicate llama.cpp directories to save space..." && \
    rm -rf /app/server-12/third_party && \
    rm -rf /app/server-13/third_party && \
    rm -rf /app/server-13-kvcache/third_party && \
    rm -rf /app/server-14/third_party && \
    echo "Cleanup completed!"

# 编译 llama.cpp（从 server-11 的 third_party，供所有 LLM server 使用）
WORKDIR /app/server-11/third_party/llama.cpp
RUN echo "Building llama.cpp..." && \
    mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DLLAMA_CURL=OFF && \
    cmake --build . --config Release -j$(nproc) && \
    echo "llama.cpp build completed!"

# 编译 server-11 (LLM)
WORKDIR /app/server-11
RUN if [ -f main.cpp ]; then \
    echo "Building Server-11 (LLM)..." && \
    g++ main.cpp -o server11 \
    -I/app/server-11/third_party/llama.cpp/include \
    -I/app/server-11/third_party/llama.cpp/ggml/include \
    -I/app/server-11/third_party/llama.cpp/common \
    -L/app/server-11/third_party/llama.cpp/build/bin \
    -lllama -lggml -lsqlite3 -lpthread -std=c++11 && \
    echo "Server-11 build completed!"; \
    fi

# 编译 server-12 (MultiChat)
WORKDIR /app/server-12
RUN if [ -f src/main.cpp ]; then \
    echo "Building Server-12 (MultiChat)..." && \
    g++ $(find src -name "*.cpp") -o server12 \
    -I./include \
    -I./src \
    -I/app/server-11/third_party/llama.cpp/include \
    -I/app/server-11/third_party/llama.cpp/ggml/include \
    -I/app/server-11/third_party/llama.cpp/common \
    -L/app/server-11/third_party/llama.cpp/build/bin \
    -lllama -lggml -lsqlite3 -lpthread -std=c++17 && \
    echo "Server-12 build completed!"; \
    fi

# 编译 server-13-kvcache (KV Cache 优化版)
WORKDIR /app/server-13-kvcache
RUN if [ -f src/main.cpp ]; then \
    echo "Building Server-13-KVcache (KV Cache Optimized)..." && \
    g++ $(find src -name "*.cpp") -o server13-kvcache \
    -I./include \
    -I./src \
    -I/app/server-11/third_party/llama.cpp/include \
    -I/app/server-11/third_party/llama.cpp/ggml/include \
    -I/app/server-11/third_party/llama.cpp/common \
    -L/app/server-11/third_party/llama.cpp/build/bin \
    -lllama -lggml -lsqlite3 -lpthread -std=c++17 && \
    echo "Server-13-KVcache build completed!"; \
    fi

# 编译 server-13 (Batch)
WORKDIR /app/server-13
RUN if [ -f src/main.cpp ]; then \
    echo "Building Server-13 (ContextPool)..." && \
    g++ $(find src -name "*.cpp" ! -name "*_v2.cpp") -o server13 \
    -I./include \
    -I./src \
    -I/app/server-11/third_party/llama.cpp/include \
    -I/app/server-11/third_party/llama.cpp/ggml/include \
    -I/app/server-11/third_party/llama.cpp/common \
    -L/app/server-11/third_party/llama.cpp/build/bin \
    -lllama -lggml -lsqlite3 -lpthread -std=c++17 && \
    echo "Server-13 build completed!"; \
    fi

# 编译 server-14
WORKDIR /app/server-14
RUN if [ -f src/main.cpp ]; then \
    echo "Building Server-14..." && \
    g++ $(find src -name "*.cpp" ! -name "*_v2.cpp") -o server14 \
    -I./include \
    -I./src \
    -I/app/server-11/third_party/llama.cpp/include \
    -I/app/server-11/third_party/llama.cpp/ggml/include \
    -I/app/server-11/third_party/llama.cpp/common \
    -L/app/server-11/third_party/llama.cpp/build/bin \
    -lllama -lggml -lsqlite3 -lcrypto -lssl -lpthread -std=c++17 && \
    echo "Server-14 build completed!"; \
    fi

# 编译其他简单 server
WORKDIR /app/server-7
RUN if [ -f main.cpp ]; then \
    echo "Building Server-7 (Router)..." && \
    g++ main.cpp -o server7 -lsqlite3 -lpthread -std=c++11 && \
    echo "Server-7 build completed!"; \
    fi

WORKDIR /app/server-8
RUN if [ -f main.cpp ]; then \
    echo "Building Server-8 (UI)..." && \
    g++ main.cpp -o server8 -lsqlite3 -lpthread -std=c++11 && \
    echo "Server-8 build completed!"; \
    fi

WORKDIR /app/server-10
RUN if [ -f main.cpp ]; then \
    echo "Building Server-10 (JSON)..." && \
    g++ main.cpp -o server10 -lsqlite3 -lpthread -std=c++17 && \
    echo "Server-10 build completed!"; \
    fi

WORKDIR /app/server-10-1
RUN if [ -f main.cpp ]; then \
    echo "Skipping Server-10-1 (requires MongoDB C++ driver mongocxx)..." && \
    echo "Server-10-1 build skipped!"; \
    fi
# RUN if [ -f main.cpp ]; then \
#     echo "Building Server-10-1 (MongoDB)..." && \
#     g++ main.cpp -o server10-1 -lsqlite3 -lmongoc-1.0 -lpthread -std=c++11 && \
#     echo "Server-10-1 build completed!"; \
#     fi

# 创建模型目录
RUN mkdir -p /app/models

# 设置环境变量
ENV MODEL_PATH=/app/models/model.gguf
ENV LD_LIBRARY_PATH=/app/server-11/third_party/llama.cpp/build/bin:${LD_LIBRARY_PATH}

# 复制启动脚本
COPY start-server.sh /app/start-server.sh
RUN chmod +x /app/start-server.sh

# 回到根目录
WORKDIR /app

# 暴露常用端口
EXPOSE 8080 8081 8082 8083 8084

# 默认进入交互式 shell
CMD ["/bin/bash"]