#!/bin/bash

# 创建 Docker 网络（如果尚未存在）
docker network create mynetwork1 || true


# 启动 MongoDB 容器
docker run -d --network mynetwork -p 27017:27017 mongo

# 运行您的应用程序（根据您的需求进行修改）
# 例如，如果您的应用也是一个 Docker 容器，可以这样启动
 docker run -it --network mynetwork -p 9002:80 -p 9003:8080 my-cpp-server1 bash
