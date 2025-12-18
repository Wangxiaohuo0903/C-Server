# Server-11 真实AI推理 Dockerfile
# 编译llama.cpp + Server-11 + 模型下载

FROM ubuntu:22.04

# 设置非交互模式
ENV DEBIAN_FRONTEND=noninteractive

# 安装构建工具和依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

# 创建工作目录
WORKDIR /app

# 复制third_party目录（包含llama.cpp）
COPY third_party/ /app/third_party/

# 编译llama.cpp
WORKDIR /app/third_party/llama.cpp
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --config Release -j4 && \
    ls -lh libllama.so libllama.a

# 复制Server-11源代码
WORKDIR /app
COPY *.h /app/
COPY *.cpp /app/

# 编译Server-11
RUN g++ main.cpp -o server11 \
    -I/app/third_party/llama.cpp/include \
    -I/app/third_party/llama.cpp/common \
    -L/app/third_party/llama.cpp/build \
    -lllama \
    -lsqlite3 \
    -lpthread \
    -std=c++11 \
    && ls -lh server11

# 创建模型目录
RUN mkdir -p /app/models

# 下载TinyLlama模型（Q4量化版本，约600MB）
# 注意：如果网络较慢，可以注释掉这行，后续手动挂载模型
RUN echo "Downloading TinyLlama model..." && \
    cd /app/models && \
    wget -O tinyllama-q4.gguf \
    https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf \
    2>&1 | grep -E '(saved|Downloaded|ERROR)' || \
    echo "Model download failed or skipped - you can mount it manually"

# 设置环境变量
ENV MODEL_PATH=/app/models/tinyllama-q4.gguf
ENV LD_LIBRARY_PATH=/app/third_party/llama.cpp/build:$LD_LIBRARY_PATH

# 暴露端口
EXPOSE 8080

# 启动命令
CMD ["./server11"]
