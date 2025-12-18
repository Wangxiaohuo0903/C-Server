# Server-12 Docker镜像
FROM ubuntu:22.04

# 设置工作目录
WORKDIR /app

# 安装依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libsqlite3-dev \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 克隆并编译llama.cpp
RUN git clone https://github.com/ggerganov/llama.cpp /llama.cpp && \
    cd /llama.cpp && \
    mkdir build && cd build && \
    cmake .. && \
    make -j$(nproc)

# 创建models目录
RUN mkdir -p /app/models

# 复制源代码
COPY *.h *.cpp /app/

# 编译Server-12
RUN g++ /app/main.cpp -o /app/server12 \
    -I/llama.cpp \
    -L/llama.cpp/build \
    -lllama \
    -lsqlite3 \
    -lpthread \
    -std=c++11

# 暴露端口
EXPOSE 8080

# 设置环境变量
ENV MODEL_PATH=/app/models/model.gguf

# 启动命令
CMD ["/app/server12"]
