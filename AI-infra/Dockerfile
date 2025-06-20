# 基础镜像
FROM ubuntu:22.04



# 安装依赖
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      build-essential \
      python3 python3-pip \
      libssl-dev libsqlite3-dev \
      nginx apache2-utils \
      curl sqlite3 gdb llvm && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# 拷贝 nginx 配置和代码
COPY nginx.conf /etc/nginx/nginx.conf
COPY . /usr/src/myapp
WORKDIR /usr/src/myapp
RUN mkdir -p /var/cache/nginx

# 暴露端口
EXPOSE 80 8080 8081 8082

# 启动脚本
COPY start.sh /start.sh
RUN chmod +x /start.sh
CMD ["/start.sh"]
