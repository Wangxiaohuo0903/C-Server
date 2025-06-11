#!/bin/bash
set -e

# 清理旧文件
# rm -rf mongo-c-driver-* mongo-cxx-driver-*

# （可选）安装依赖
apt-get update && apt-get install -y \
    git wget cmake \
    libssl-dev libsasl2-dev \
    libboost-all-dev python3 && \
    rm -rf /var/lib/apt/lists/*

########################################
# 安装 MongoDB C 驱动 1.25.0
########################################
echo "=== 安装 MongoDB C 驱动 (1.25.0) ==="
# wget -O mongo-c-driver-1.25.0.tar.gz \
#      https://github.com/mongodb/mongo-c-driver/archive/refs/tags/1.25.0.tar.gz
tar -xzf mongo-c-driver-1.25.0.tar.gz
cd mongo-c-driver-1.25.0 || { echo "进入 C 驱动源码目录失败"; exit 1; }

# 方案一：手动生成版本文件
echo "1.25.0" > VERSION_CURRENT

# 方案二：或在 cmake 命令中加 -DBUILD_VERSION=1.25.0
# （可任选其一，不要同时使用两者）

mkdir -p build && cd build || { echo "创建构建目录失败"; exit 1; }
cmake .. \
    -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF \
    -DENABLE_BSON=ON \
    -DENABLE_TESTS=OFF \
    #-DBUILD_VERSION=1.25.0
make -j$(nproc)
make install
ldconfig
cd ../..

########################################
# 安装 MongoDB C++ 驱动 r3.9.0
########################################
echo "=== 安装 MongoDB C++ 驱动 (r3.9.0) ==="
# wget -O mongo-cxx-driver-r3.9.0.tar.gz \
#      https://github.com/mongodb/mongo-cxx-driver/archive/r3.9.0.tar.gz
tar -xzf mongo-cxx-driver-r3.9.0.tar.gz

cd mongo-cxx-driver-r3.9.0/build && \
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DBSONCXX_POLY_USE_BOOST=1 \
         -DMONGOCXX_OVERRIDE_DEFAULT_INSTALL_PREFIX=OFF \
         -DBUILD_VERSION=3.9.0
cmake --build .
cmake --build . --target install

echo "=== 全部安装完成 ==="
