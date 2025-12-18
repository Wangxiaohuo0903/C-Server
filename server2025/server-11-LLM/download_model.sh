#!/bin/bash
# Server-11 模型下载脚本
# 下载TinyLlama-1.1B-Chat Q4量化模型

set -e

MODEL_DIR="models"
MODEL_FILE="tinyllama-q4.gguf"
MODEL_URL="https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/main/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf"

echo "==================================="
echo "Server-11 模型下载工具"
echo "==================================="
echo ""

# 创建models目录
if [ ! -d "$MODEL_DIR" ]; then
    echo "✅ 创建models目录..."
    mkdir -p "$MODEL_DIR"
fi

# 检查模型是否已存在
if [ -f "$MODEL_DIR/$MODEL_FILE" ]; then
    echo "⚠️  模型文件已存在: $MODEL_DIR/$MODEL_FILE"
    read -p "是否重新下载? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "跳过下载"
        exit 0
    fi
    rm -f "$MODEL_DIR/$MODEL_FILE"
fi

# 下载模型
echo "📥 开始下载模型 (约600MB)..."
echo "下载地址: $MODEL_URL"
echo ""

# 使用wget下载（带进度条）
if command -v wget &> /dev/null; then
    wget -O "$MODEL_DIR/$MODEL_FILE" "$MODEL_URL"
# 使用curl下载（备选）
elif command -v curl &> /dev/null; then
    curl -L -o "$MODEL_DIR/$MODEL_FILE" "$MODEL_URL"
else
    echo "❌ 错误: 未找到wget或curl命令"
    echo "请手动下载模型文件到 $MODEL_DIR/$MODEL_FILE"
    exit 1
fi

# 检查下载结果
if [ -f "$MODEL_DIR/$MODEL_FILE" ]; then
    FILE_SIZE=$(du -h "$MODEL_DIR/$MODEL_FILE" | cut -f1)
    echo ""
    echo "✅ 模型下载成功!"
    echo "文件路径: $MODEL_DIR/$MODEL_FILE"
    echo "文件大小: $FILE_SIZE"
    echo ""
    echo "现在可以运行Server-11了:"
    echo "  方式1 (Docker): docker-compose up"
    echo "  方式2 (直接运行): ./server11"
else
    echo "❌ 模型下载失败"
    echo "请检查网络连接或手动下载"
    exit 1
fi
