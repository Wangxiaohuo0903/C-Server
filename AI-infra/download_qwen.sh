#!/bin/bash
# 下载Qwen2.5-1.5B-Instruct模型（推荐，中文效果最好）

echo "=== 开始下载Qwen2.5-1.5B-Instruct模型 ==="
echo "大小: 约1GB，下载时间根据网速约5-20分钟"
echo ""

cd "$(dirname "$0")/models"

# 方法1: 使用huggingface-cli（需要先安装）
echo "方法1: 使用huggingface-cli下载"
echo "pip install -U huggingface_hub"
echo "huggingface-cli download Qwen/Qwen2.5-1.5B-Instruct-GGUF qwen2.5-1.5b-instruct-q4_0.gguf --local-dir . --local-dir-use-symlinks False"
echo ""

# 方法2: 直接wget（可能需要代理）
echo "方法2: 使用wget直接下载"
echo "wget https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_0.gguf"
echo ""

# 方法3: 国内镜像（ModelScope）
echo "方法3: 使用ModelScope国内镜像（推荐国内用户）"
echo "pip install modelscope"
echo "modelscope download --model Qwen/Qwen2.5-1.5B-Instruct-GGUF --local_dir ."
echo ""

echo "下载完成后，修改docker-compose.yml中的MODEL_PATH:"
echo "MODEL_PATH=/app/models/qwen2.5-1.5b-instruct-q4_0.gguf"
