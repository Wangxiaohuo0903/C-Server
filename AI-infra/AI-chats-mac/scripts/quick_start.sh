#!/bin/bash
# LoRA蒸馏训练快速启动脚本

set -e

echo "=================================================="
echo "  LoRA蒸馏训练 - 快速启动"
echo "=================================================="

# 检查Python
if ! command -v python3 &> /dev/null; then
    echo "❌ 未找到Python3，请先安装"
    exit 1
fi

# 创建虚拟环境
if [ ! -d "venv" ]; then
    echo "📦 创建虚拟环境..."
    python3 -m venv venv
fi

# 激活虚拟环境
echo "🔧 激活虚拟环境..."
source venv/bin/activate

# 安装依赖
echo "📥 安装依赖..."
pip install -q --upgrade pip
pip install -q -r requirements_distill.txt

echo ""
echo "✅ 环境准备完成!"
echo ""
echo "=================================================="
echo "  选择训练模式:"
echo "=================================================="
echo ""
echo "1. 快速测试 (5k样本, 1-2小时, 免费)"
echo "2. 标准训练 (20k样本, 4-6小时, 推荐)"
echo "3. 生产级 (100k样本, 6-8小时, 最佳效果)"
echo "4. 自定义参数"
echo ""
read -p "请选择 (1-4): " choice

case $choice in
    1)
        echo ""
        echo "🚀 启动快速测试模式..."
        python train_lora_distill.py \
            --output_dir ./lora-test \
            --max_samples 5000 \
            --num_epochs 1 \
            --batch_size 2 \
            --gradient_accumulation_steps 4
        ;;
    2)
        echo ""
        echo "🚀 启动标准训练模式..."
        python train_lora_distill.py \
            --output_dir ./lora-distilled \
            --max_samples 20000 \
            --num_epochs 2 \
            --batch_size 2 \
            --gradient_accumulation_steps 4 \
            --use_wandb
        ;;
    3)
        echo ""
        echo "🚀 启动生产级训练..."
        python train_lora_distill.py \
            --output_dir ./lora-prod \
            --dataset bigcode/the-stack \
            --max_samples 100000 \
            --num_epochs 3 \
            --batch_size 4 \
            --gradient_accumulation_steps 4 \
            --lora_r 32 \
            --use_wandb
        ;;
    4)
        echo ""
        echo "请手动运行:"
        echo "python train_lora_distill.py --help"
        ;;
    *)
        echo "❌ 无效选择"
        exit 1
        ;;
esac

echo ""
echo "=================================================="
echo "  训练完成!"
echo "=================================================="
echo ""
echo "下一步:"
echo "1. 查看模型: ls -lh ./lora-*/merged/"
echo "2. 转换GGUF: 参考 README_LORA_DISTILL.md"
echo "3. 测试性能: ./build/test_k_optimization"
echo ""
