# Server-12 Multi-Chat Dockerfile
# 多阶段构建，优化镜像大小

# ========== 阶段1：编译llama.cpp ==========
FROM ubuntu:22.04 AS llama-builder

ENV DEBIAN_FRONTEND=noninteractive

# 使用阿里云镜像源（解决网络问题）
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list && \
    sed -i 's@//.*security.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list

# 安装编译工具
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

# 复制llama.cpp源码（从AI-infra）
WORKDIR /build
COPY third_party/llama.cpp /build/llama.cpp

# 编译llama.cpp (禁用CURL以简化依赖)
RUN cd llama.cpp && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DLLAMA_CURL=OFF && \
    cmake --build build --config Release -j$(nproc) && \
    echo "=== llama.cpp build structure ===" && \
    find build -name "*.a" -o -name "*.so" -o -name "libllama*" -o -name "libggml*" | head -20

# ========== 阶段2：构建Server-12 ==========
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 使用阿里云镜像源（解决网络问题）
RUN sed -i 's@//.*archive.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list && \
    sed -i 's@//.*security.ubuntu.com@//mirrors.aliyun.com@g' /etc/apt/sources.list

# 安装运行时依赖和编译工具
RUN apt-get update && apt-get install -y \
    build-essential \
    libsqlite3-dev \
    libgomp1 \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 创建工作目录
WORKDIR /app

# 从构建阶段复制llama.cpp库
COPY --from=llama-builder /build/llama.cpp/build/bin /app/third_party/llama.cpp/build/bin
COPY --from=llama-builder /build/llama.cpp/include /app/third_party/llama.cpp/include
COPY --from=llama-builder /build/llama.cpp/ggml/include /app/third_party/llama.cpp/ggml/include

# 复制Server-12源码
COPY include/ /app/include/
COPY src/ /app/src/
COPY UI/ /app/UI/
COPY users.db /app/

# 编译Server-12
RUN g++ src/main.cpp src/inference/ModelManager.cpp -o server12 \
    -I/app/include \
    -I/app/src \
    -I/app/third_party/llama.cpp/include \
    -I/app/third_party/llama.cpp/ggml/include \
    -L/app/third_party/llama.cpp/build/bin \
    -Wl,-rpath,/app/third_party/llama.cpp/build/bin \
    -lllama \
    -lggml \
    -lsqlite3 \
    -lpthread \
    -std=c++17 \
    -O2

# 设置库路径
ENV LD_LIBRARY_PATH=/app/third_party/llama.cpp/build/bin:${LD_LIBRARY_PATH}

# 暴露端口
EXPOSE 8080

# 启动命令
CMD ["./server12"]
