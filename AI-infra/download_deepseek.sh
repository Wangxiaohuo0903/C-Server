#!/bin/bash
# 下载DeepSeek-R1-Distill-Qwen-1.5B模型（推理能力强）

echo "=== 开始下载DeepSeek-R1-Distill-Qwen-1.5B模型 ==="
echo "大小: 约1GB，这是DeepSeek的蒸馏模型，推理能力强"
echo ""

cd "$(dirname "$0")/models"

# 方法1: HuggingFace下载
echo "方法1: 使用huggingface-cli下载"
echo "huggingface-cli download deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF deepseek-r1-distill-qwen-1.5b-q4_k_m.gguf --local-dir . --local-dir-use-symlinks False"
echo ""

# 方法2: ModelScope镜像（国内推荐）
echo "方法2: 使用ModelScope国内镜像"
echo "modelscope download --model deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B --local_dir ."
echo ""

# 方法3: 直接下载链接
echo "方法3: 浏览器下载"
echo "https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B-GGUF/resolve/main/deepseek-r1-distill-qwen-1.5b-q4_k_m.gguf"
echo ""

echo "下载完成后，修改docker-compose.yml中的MODEL_PATH:"
echo "MODEL_PATH=/app/models/deepseek-r1-distill-qwen-1.5b-q4_k_m.gguf"
