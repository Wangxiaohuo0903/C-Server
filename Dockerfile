# 使用特定版本的Ubuntu镜像
FROM ubuntu:latest

# 更新软件包和安装所需的库
RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential python3 python3-pip libsqlite3-dev && \
    rm -rf /var/lib/apt/lists/*

# 复制代码到容器中
COPY . /usr/src/myapp

# 设置工作目录
WORKDIR /usr/src/myapp

# 编译程序
# 确保您的编译命令适用于您的项目
RUN g++ -o myserver10 lessons/lesson12/main.cpp -lsqlite3

# 运行服务器
CMD ["bash"]  # 或者您希望运行的其他命令
