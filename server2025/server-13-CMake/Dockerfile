FROM ubuntu:22.04

# 设置工作目录
WORKDIR /app

# 安装构建工具和依赖
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    libsqlite3-dev \
    curl \
    && rm -rf /var/lib/apt/lists/*

# 复制项目文件
COPY CMakeLists.txt /app/
COPY include/ /app/include/
COPY src/ /app/src/
COPY README.md /app/

# 创建build目录并编译
RUN mkdir -p build && cd build && \
    cmake .. && \
    make && \
    ls -lh server13

# 设置环境变量使用mock模型（测试模式）
ENV MODEL_PATH=/app/models/mock.gguf

# 暴露端口
EXPOSE 8080

# 启动服务
CMD ["./build/server13"]
