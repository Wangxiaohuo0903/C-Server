#!/bin/bash

# Mac 版本构建脚本
# 使用方法: ./build.sh [clean|rebuild]

set -e  # 遇到错误立即退出

echo "================================"
echo "AI-Infra Server Mac 构建脚本"
echo "================================"

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 获取 CPU 核心数
CPU_CORES=$(sysctl -n hw.ncpu)

# 检查参数
if [ "$1" == "clean" ]; then
    echo -e "${YELLOW}清理 build 目录...${NC}"
    rm -rf build
    echo -e "${GREEN}清理完成${NC}"
    exit 0
fi

if [ "$1" == "rebuild" ]; then
    echo -e "${YELLOW}清理并重新构建...${NC}"
    rm -rf build
fi

# 创建 build 目录
if [ ! -d "build" ]; then
    echo -e "${GREEN}创建 build 目录...${NC}"
    mkdir -p build
fi

cd build

# CMake 配置
echo -e "${GREEN}运行 CMake 配置...${NC}"
cmake .. || {
    echo -e "${RED}CMake 配置失败！${NC}"
    exit 1
}

# 编译
echo -e "${GREEN}开始编译 (使用 ${CPU_CORES} 个 CPU 核心)...${NC}"
make -j${CPU_CORES} || {
    echo -e "${RED}编译失败！${NC}"
    exit 1
}

# 检查编译结果
if [ -f "ai_infra_server_mac" ]; then
    echo -e "${GREEN}================================${NC}"
    echo -e "${GREEN}编译成功！${NC}"
    echo -e "${GREEN}================================${NC}"
    echo ""
    echo "可执行文件位置: $(pwd)/ai_infra_server_mac"
    echo "文件大小: $(ls -lh ai_infra_server_mac | awk '{print $5}')"
    echo "架构: $(file ai_infra_server_mac | cut -d':' -f2)"
    echo ""
    echo -e "${YELLOW}运行服务器:${NC}"
    echo "  ./ai_infra_server_mac"
    echo ""
    echo -e "${YELLOW}注意事项:${NC}"
    echo "  1. 确保已配置模型文件路径 (src/main.cpp:11)"
    echo "  2. 确保模型文件存在于指定路径"
    echo "  3. 服务器将在 8080 端口启动"
else
    echo -e "${RED}编译失败：未找到可执行文件${NC}"
    exit 1
fi
