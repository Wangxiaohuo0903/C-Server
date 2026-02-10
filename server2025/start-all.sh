#!/bin/bash
# Server2025 统一启动脚本
#
# 使用方法:
#   ./start-all.sh          # 启动所有服务器
#   ./start-all.sh build    # 重新构建并启动
#   ./start-all.sh stop     # 停止所有服务器
#   ./start-all.sh status   # 查看状态
#   ./start-all.sh logs     # 查看日志

cd "$(dirname "$0")"

case "$1" in
  build)
    echo "🔨 构建并启动所有服务器..."
    docker compose -f docker-compose.all.yml up -d --build
    ;;
  stop)
    echo "🛑 停止所有服务器..."
    docker compose -f docker-compose.all.yml down
    ;;
  status)
    echo "📊 服务器状态:"
    docker compose -f docker-compose.all.yml ps
    ;;
  logs)
    echo "📜 查看日志 (Ctrl+C 退出)..."
    docker compose -f docker-compose.all.yml logs -f
    ;;
  restart)
    echo "🔄 重启所有服务器..."
    docker compose -f docker-compose.all.yml restart
    ;;
  *)
    echo "🚀 启动所有服务器..."
    docker compose -f docker-compose.all.yml up -d
    echo ""
    echo "✅ 服务器已启动:"
    echo "   - Server-12 (基础多轮对话):  http://localhost:6060"
    echo "   - Server-13 (KV缓存优化):    http://localhost:6061"
    echo "   - Server-14 (用户认证):      http://localhost:6062"
    echo "   - Server-14-Batch (批处理):  http://localhost:6063"
    echo ""
    echo "📊 查看状态: ./start-all.sh status"
    echo "🛑 停止服务: ./start-all.sh stop"
    ;;
esac
