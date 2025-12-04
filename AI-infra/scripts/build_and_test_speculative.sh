#!/bin/bash

# ============================================================
# 编译并测试推测式解码
# ============================================================

set -e

echo "🔨 Building Speculative Decoding Test..."
echo ""

# 进入项目目录
cd "$(dirname "$0")/../AI-chats-linux"

# 创建 build 目录
mkdir -p build
cd build

# CMake 配置
echo "⚙️  Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
echo "🏗️  Compiling..."
make -j$(nproc)

echo ""
echo "✅ Build complete!"
echo ""

# 检查模型是否存在
DRAFT_MODEL="../models/draft/tinyllama-160m-q4.gguf"
TARGET_MODEL="../models/tinyllama-1.1b-q4.gguf"

if [ ! -f "$DRAFT_MODEL" ]; then
    echo "❌ Draft model not found: $DRAFT_MODEL"
    echo "Please run: scripts/download_draft_models.sh"
    exit 1
fi

if [ ! -f "$TARGET_MODEL" ]; then
    echo "⚠️  Target model not found: $TARGET_MODEL"
    echo "Please download TinyLlama-1.1B model first"
    exit 1
fi

echo "📊 Running test..."
echo ""

# 运行测试
./test_speculative \
    --model "$TARGET_MODEL" \
    --model-draft "$DRAFT_MODEL" \
    --prompt "用Python实现快速排序算法" \
    --n-predict 100 \
    --n-draft 16 \
    --temperature 0.7 \
    --verbose

echo ""
echo "🎉 Test complete!"
