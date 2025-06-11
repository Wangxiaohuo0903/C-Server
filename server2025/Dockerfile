# 使用特定版本的Ubuntu镜像
FROM ubuntu:22.04

# 阶段1：使用默认官方源安装证书工具
# 注意：此处暂不替换镜像源，利用Docker镜像内置的源配置
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      ca-certificates \
      gnupg2 && \
    rm -rf /var/lib/apt/lists/*

# 阶段2：替换为阿里云镜像源
RUN sed -i \
    -e 's|http://archive.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g' \
    -e 's|http://security.ubuntu.com/ubuntu|https://mirrors.aliyun.com/ubuntu|g' \
    /etc/apt/sources.list

# 阶段3：更新并安装其他依赖
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential \
      python3 python3-pip libssl-dev libsqlite3-dev \
      nginx apache2-utils curl sqlite3 && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# 后续配置保持不变...
COPY nginx.conf /etc/nginx/nginx.conf
COPY . /usr/src/myapp
WORKDIR /usr/src/myapp
RUN mkdir -p /var/cache/nginx
EXPOSE 80 8080 8081 8082
COPY start.sh /start.sh
RUN chmod +x /start.sh
CMD ["/start.sh"]