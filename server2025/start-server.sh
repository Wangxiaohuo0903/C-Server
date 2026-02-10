#!/bin/bash

# Server 启动脚本 - 交互式选择运行哪个 server

echo "===================================="
echo "   Server 2025 - 统一启动脚本"
echo "===================================="
echo ""
echo "可用的 Server 版本："
echo ""
echo "  1)  Server-1  - Hello World"
echo "  2)  Server-2  - HTTP 基础"
echo "  3)  Server-3  - Logger 日志系统"
echo "  4)  Server-4  - Database 数据库"
echo "  5)  Server-5  - Epoll I/O 多路复用"
echo "  6)  Server-6  - Thread Pool 线程池"
echo "  7)  Server-7  - Router 路由系统"
echo "  8)  Server-8  - UI 用户界面"
echo "  9)  Server-9  - Nginx 反向代理"
echo "  10) Server-10 - JSON 处理"
echo "  11) Server-10-1 - MongoDB 集成"
echo "  12) Server-11-1 - SSL/TLS 加密"
echo "  13) Server-11 - LLM 推理服务 ⭐"
echo "  14) Server-12 - MultiChat 多轮对话"
echo "  15) Server-13 - ContextPool 上下文池"
echo "  16) Server-13-Batch - Batch 批处理"
echo "  17) Server-13-KV - KV Cache 优化"
echo "  18) Server-14 - DeepSeek-R1 推理链"
echo ""
echo "  q)  退出"
echo ""

# 如果有参数，直接运行指定的 server
if [ -n "$1" ]; then
    SERVER_NUM=$1
else
    read -p "请选择要启动的 Server (输入数字): " SERVER_NUM
fi

case $SERVER_NUM in
    1)
        echo "启动 Server-1 (Hello World)..."
        cd /app/server-1 && ./server1 2>&1
        ;;
    2)
        echo "启动 Server-2 (HTTP)..."
        cd /app/server-2 && ./server2 2>&1
        ;;
    3)
        echo "启动 Server-3 (Logger)..."
        cd /app/server-3 && ./server3 2>&1
        ;;
    4)
        echo "启动 Server-4 (Database)..."
        cd /app/server-4 && ./server4 2>&1
        ;;
    5)
        echo "启动 Server-5 (Epoll)..."
        cd /app/server-5 && ./server5 2>&1
        ;;
    6)
        echo "启动 Server-6 (Thread Pool)..."
        cd /app/server-6 && ./server6 2>&1
        ;;
    7)
        echo "启动 Server-7 (Router)..."
        cd /app/server-7 && ./server7 2>&1
        ;;
    8)
        echo "启动 Server-8 (UI)..."
        cd /app/server-8 && ./server8 2>&1
        ;;
    9)
        echo "启动 Server-9 (Nginx)..."
        cd /app/server-9 && ./server9 2>&1
        ;;
    10)
        echo "启动 Server-10 (JSON)..."
        cd /app/server-10 && ./server10 2>&1
        ;;
    11)
        echo "启动 Server-10-1 (MongoDB)..."
        cd /app/server-10-1 && ./server10-1 2>&1
        ;;
    12)
        echo "启动 Server-11-1 (SSL/TLS)..."
        cd /app/server-11-1 && ./server11-1 2>&1
        ;;
    13)
        echo "启动 Server-11 (LLM 推理服务)..."
        echo "模型路径: $MODEL_PATH"
        if [ ! -f "$MODEL_PATH" ]; then
            echo "⚠️  警告: 模型文件不存在: $MODEL_PATH"
            echo "请使用 -v 挂载模型目录，或设置 MODEL_PATH 环境变量"
            echo ""
            echo "示例："
            echo "  docker run -v ~/Downloads:/app/models -e MODEL_PATH=/app/models/your-model.gguf ..."
            exit 1
        fi
        cd /app/server-11 && ./server11 2>&1
        ;;
    14)
        echo "启动 Server-12 (MultiChat)..."
        echo "模型路径: $MODEL_PATH"
        if [ ! -f "$MODEL_PATH" ]; then
            echo "⚠️  警告: 模型文件不存在: $MODEL_PATH"
            exit 1
        fi
        cd /app/server-12 && ./server12 2>&1
        ;;
    15)
        echo "启动 Server-13 (ContextPool)..."
        echo "模型路径: $MODEL_PATH"
        if [ ! -f "$MODEL_PATH" ]; then
            echo "⚠️  警告: 模型文件不存在: $MODEL_PATH"
            exit 1
        fi
        cd /app/server-13 && ./server13 2>&1
        ;;
    16)
        echo "启动 Server-13-Batch (批处理)..."
        cd /app/server-13-batch && ./server13-batch 2>&1
        ;;
    17)
        echo "启动 Server-13-KV (KV Cache)..."
        cd /app/server-13-kv && ./server13-kv 2>&1
        ;;
    18)
        echo "启动 Server-14 (DeepSeek-R1)..."
        echo "模型路径: $MODEL_PATH"
        if [ ! -f "$MODEL_PATH" ]; then
            echo "⚠️  警告: 模型文件不存在: $MODEL_PATH"
            exit 1
        fi
        cd /app/server-14 && ./server14 2>&1
        ;;
    q|Q)
        echo "退出..."
        exit 0
        ;;
    *)
        echo "❌ 无效的选择: $SERVER_NUM"
        exit 1
        ;;
esac
