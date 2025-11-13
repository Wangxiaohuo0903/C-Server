# =============================================================================
# AI-Chats-Linux Docker 镜像
# 用于在 Linux 容器中编译和运行 AI 推理服务器
# =============================================================================

FROM ubuntu:22.04

# 设置环境变量避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Asia/Shanghai

# 安装编译依赖和运行时库
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        # 编译工具链
        build-essential \
        cmake \
        git \
        # C++ 依赖库
        libssl-dev \
        libsqlite3-dev \
        # 实用工具
        curl \
        sqlite3 \
        ca-certificates \
        # 清理缓存减小镜像体积
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# 设置工作目录
WORKDIR /app

# 复制项目文件（利用 Docker 缓存层）
# 先复制依赖配置文件
COPY AI-chats-linux/CMakeLists.txt ./AI-chats-linux/

# 克隆 llama.cpp (因为本地子模块未初始化)
RUN git clone --depth 1 https://github.com/ggerganov/llama.cpp.git ./third_party/llama.cpp

# 复制源代码
COPY AI-chats-linux/include/ ./AI-chats-linux/include/
COPY AI-chats-linux/src/ ./AI-chats-linux/src/
COPY AI-chats-linux/build.sh ./AI-chats-linux/

# 复制配置和数据文件
COPY AI-chats-linux/warmup_templates_code.json ./AI-chats-linux/
COPY AI-chats-linux/*.md ./AI-chats-linux/

# 创建必要的目录
RUN mkdir -p models AI-chats-linux/build

# 编译项目
# 注意：由于代码使用了 kqueue (Mac/BSD)，在 Linux 上需要改为 epoll
# 这里假设已经适配了 Linux，或者使用条件编译
WORKDIR /app/AI-chats-linux
RUN chmod +x build.sh && \
    # 使用所有可用 CPU 核心加速编译
    ./build.sh || \
    (cd build && cmake .. && make -j$(nproc))

# 创建用户数据库目录
RUN mkdir -p /data && \
    touch /data/users.db

# 暴露服务端口
EXPOSE 8080

# 健康检查
HEALTHCHECK --interval=30s --timeout=3s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/ || exit 1

# 设置启动命令 - 直接运行可执行文件
WORKDIR /app/AI-chats-linux/build
CMD ["./ai_infra_server_mac"]

# =============================================================================
# 构建和运行示例:
#
# 从项目根目录构建:
#   docker build -f AI-chats-linux/Dockerfile -t ai-chats-linux:latest .
#
# 运行容器:
#   docker run -d \
#     -p 8080:8080 \
#     -v $(pwd)/models:/app/models:ro \
#     -v ai-chats-data:/data \
#     --name ai-chats \
#     ai-chats-linux:latest
#
# 查看日志:
#   docker logs -f ai-chats
#
# 进入容器:
#   docker exec -it ai-chats /bin/bash
# =============================================================================
