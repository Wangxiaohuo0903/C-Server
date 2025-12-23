# ============================================================
# AI-chats Dockerfile
# ============================================================
# 基于 Ubuntu 22.04，编译并运行 AI-chats 服务器
#
# 构建命令:
#   docker build -t ai-chats:latest .
#
# 运行命令:
#   docker run -p 8080:8080 -v /path/to/models:/app/models ai-chats:latest
# ============================================================

# ========== 阶段 1: 构建阶段 ==========
FROM ubuntu:22.04 AS builder

# 设置非交互式安装（避免 tzdata 等包卡住）
ENV DEBIAN_FRONTEND=noninteractive

# 替换为阿里云镜像源（加速下载）
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libsqlite3-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建工作目录
WORKDIR /build

# 先克隆 llama.cpp（避免路径问题）
RUN mkdir -p /build/third_party && \
    cd /build/third_party && \
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git

# 复制项目文件
COPY . /build/AI-chats/

# 构建项目
WORKDIR /build/AI-chats/build
RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DGGML_NATIVE=OFF \
    -DGGML_CPU_ALL_VARIANTS=OFF \
    && make -j$(nproc)

# ========== 阶段 2: 运行阶段 ==========
FROM ubuntu:22.04

# 替换为阿里云镜像源（加速下载）
RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list && \
    sed -i 's|http://security.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libsqlite3-0 \
    libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# 创建应用目录
WORKDIR /app

# 从构建阶段复制可执行文件
COPY --from=builder /build/AI-chats/build/ai_infra_server /app/

# 从构建阶段复制 llama.cpp 共享库（正确路径：build/bin/）
COPY --from=builder /build/AI-chats/build/bin/*.so* /usr/lib/

# 更新动态链接库缓存
RUN ldconfig

# 创建必要的目录
RUN mkdir -p /app/models

# 暴露端口
EXPOSE 8080

# 设置环境变量
ENV MODEL_PATH=/app/models/tinyllama-q4.gguf

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD wget --no-verbose --tries=1 --spider http://localhost:8080/ || exit 1

# 启动服务器
CMD ["./ai_infra_server"]
