#!/bin/bash

# ============================================================
# Draft 模型下载脚本
# 用于推测式解码（Speculative Decoding）
# ============================================================

set -e  # 遇到错误立即退出

MODELS_DIR="../models/draft"
mkdir -p "$MODELS_DIR"

echo "📦 Starting draft model download..."
echo "📁 Target directory: $MODELS_DIR"
echo ""

# ============================================================
# 模型选项
# ============================================================

show_menu() {
    echo "请选择要下载的 draft 模型："
    echo ""
    echo "1. TinyLlama-160M (推荐) - 极致速度，适合 CPU 推理"
    echo "   参数量: 160M"
    echo "   文件大小: ~100MB (Q4_K_M 量化)"
    echo "   适用场景: 快速 draft，接受率 40-60%"
    echo ""
    echo "2. TinyLlama-500M - 平衡速度与准确性"
    echo "   参数量: 500M"
    echo "   文件大小: ~300MB (Q4_K_M 量化)"
    echo "   适用场景: 更高接受率，略慢"
    echo ""
    echo "3. SmolLM-135M - 超小模型，极低延迟"
    echo "   参数量: 135M"
    echo "   文件大小: ~80MB (Q4_K_M 量化)"
    echo "   适用场景: 资源受限环境"
    echo ""
    echo "4. 下载全部模型（用于对比测试）"
    echo ""
    echo "0. 退出"
    echo ""
    read -p "请输入选项 [0-4]: " choice
    echo ""
}

# ============================================================
# 下载函数
# ============================================================

download_model() {
    local name=$1
    local url=$2
    local filename=$3

    echo "⬇️  Downloading $name..."
    echo "🔗 URL: $url"
    echo "📄 Filename: $filename"
    echo ""

    if [ -f "$MODELS_DIR/$filename" ]; then
        echo "⚠️  File already exists: $MODELS_DIR/$filename"
        read -p "是否覆盖? [y/N]: " overwrite
        if [[ ! "$overwrite" =~ ^[Yy]$ ]]; then
            echo "⏭️  Skipped."
            echo ""
            return
        fi
    fi

    # 使用 wget 或 curl 下载
    if command -v wget &> /dev/null; then
        wget -O "$MODELS_DIR/$filename" "$url" --progress=bar:force:noscroll
    elif command -v curl &> /dev/null; then
        curl -L -o "$MODELS_DIR/$filename" "$url" --progress-bar
    else
        echo "❌ Error: wget 或 curl 未安装"
        exit 1
    fi

    if [ $? -eq 0 ]; then
        echo "✅ Downloaded: $MODELS_DIR/$filename"
        echo "📊 File size: $(du -h "$MODELS_DIR/$filename" | cut -f1)"
    else
        echo "❌ Download failed"
        exit 1
    fi
    echo ""
}

# ============================================================
# 模型下载 URLs
# ============================================================

download_tinyllama_160m() {
    download_model \
        "TinyLlama-160M" \
        "https://huggingface.co/TinyLlama/TinyLlama-160M-Chat-v0.6-GGUF/resolve/main/tinyllama-160m-chat-v0.6.Q4_K_M.gguf" \
        "tinyllama-160m-q4.gguf"
}

download_tinyllama_500m() {
    download_model \
        "TinyLlama-500M" \
        "https://huggingface.co/TinyLlama/TinyLlama-500M-Chat-v0.6-GGUF/resolve/main/tinyllama-500m-chat-v0.6.Q4_K_M.gguf" \
        "tinyllama-500m-q4.gguf"
}

download_smollm_135m() {
    download_model \
        "SmolLM-135M" \
        "https://huggingface.co/HuggingFaceTB/SmolLM-135M-Instruct-GGUF/resolve/main/smollm-135m-instruct.Q4_K_M.gguf" \
        "smollm-135m-q4.gguf"
}

# ============================================================
# 主流程
# ============================================================

show_menu

case $choice in
    1)
        download_tinyllama_160m
        ;;
    2)
        download_tinyllama_500m
        ;;
    3)
        download_smollm_135m
        ;;
    4)
        echo "📦 Downloading all models..."
        download_tinyllama_160m
        download_tinyllama_500m
        download_smollm_135m
        ;;
    0)
        echo "👋 Bye!"
        exit 0
        ;;
    *)
        echo "❌ Invalid option"
        exit 1
        ;;
esac

# ============================================================
# 验证下载
# ============================================================

echo ""
echo "✅ Download complete!"
echo ""
echo "📂 Downloaded models:"
ls -lh "$MODELS_DIR"
echo ""
echo "🚀 Next steps:"
echo "1. 更新配置文件指定 draft 模型路径"
echo "2. 运行测试：./test_speculative_decoding.sh"
echo "3. 启动服务并使用推测式解码"
echo ""
