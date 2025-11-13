#!/bin/bash

# AI Chat Server 启动脚本
# 使用方法: ./start_server.sh

cd "$(dirname "$0")/build" || exit 1

echo "======================================"
echo "  AI Chat Server (Mac 版本)"
echo "======================================"
echo ""
echo "正在启动服务器..."
echo "服务器地址: http://localhost:8080"
echo ""
echo "提示："
echo "  - 登录页面: http://localhost:8080/login.html"
echo "  - 注册页面: http://localhost:8080/register.html"
echo ""
echo "按 Ctrl+C 停止服务器"
echo "======================================"
echo ""

# 启动服务器
./ai_infra_server_mac
