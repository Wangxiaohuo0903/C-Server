# 使用特定版本的Ubuntu镜像
FROM ubuntu:latest

# 替换为清华大学的 Ubuntu 镜像源
RUN sed -i 's/http:\/\/ports.ubuntu.com/http:\/\/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list

# 更新软件包并安装所需的库
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    build-essential \
    python3 \
    python3-pip \
    libsqlite3-dev \
    nginx \
    git \
    libevent-dev

# 安装 hiredis
RUN git clone https://github.com/redis/hiredis.git && \
    cd hiredis && \
    make && make install && \
    ldconfig

# 安装 hiredis_cluster (这个步骤可能需要根据您使用的 hiredis_cluster 库进行调整)
# 示例中未包含具体的 hiredis_cluster 安装步骤，您需要添加这一步

# 复制代码到容器中
COPY . /usr/src/myapp

# 设置工作目录
WORKDIR /usr/src/myapp

# 编译程序 (确保您的编译命令适用于您的项目)
RUN g++ -o myserver10 main.cpp -lsqlite3 -lhiredis -pthread

# 暴露端口（Nginx 和您的应用程序）
EXPOSE 80 8080 8081

# 配置 Nginx
COPY nginx.conf /etc/nginx/nginx.conf

# 创建并复制启动脚本到容器
COPY start.sh /start.sh
RUN chmod +x /start.sh

# 启动脚本将运行 Nginx 和您的应用程序
CMD ["/start.sh"]
