#!/bin/bash
# ============================================================
# 完整性能测试脚本 (Linux/macOS)
# 运行所有对比实验并生成报告
# ============================================================

set -e

echo "╔════════════════════════════════════════════════════════════╗"
echo "║       Speculative Decoding Full Benchmark Suite           ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# ============================================================
# 配置
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/../AI-chats-linux"
MODEL_DIR="$SCRIPT_DIR/../models"
TARGET_MODEL="$MODEL_DIR/tinyllama-1.1b-q4.gguf"
DRAFT_MODEL="$MODEL_DIR/draft/tinyllama-160m-q4.gguf"
OUTPUT_DIR="$SCRIPT_DIR/../benchmark_results"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# 创建输出目录
mkdir -p "$OUTPUT_DIR"

echo "📁 Configuration:"
echo "   Target Model:    $TARGET_MODEL"
echo "   Draft Model:     $DRAFT_MODEL"
echo "   Output Dir:      $OUTPUT_DIR"
echo "   Timestamp:       $TIMESTAMP"
echo ""

# 检查模型文件
if [ ! -f "$TARGET_MODEL" ]; then
    echo "❌ Target model not found: $TARGET_MODEL"
    exit 1
fi

if [ ! -f "$DRAFT_MODEL" ]; then
    echo "❌ Draft model not found: $DRAFT_MODEL"
    echo "Please run: ./download_draft_models.sh"
    exit 1
fi

# ============================================================
# 编译项目
# ============================================================

echo "🔨 Building project..."
cd "$PROJECT_DIR"

mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo "❌ CMake configuration failed"
    exit 1
fi

cmake --build . --config Release -j 4
if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi

echo "✅ Build complete"
echo ""

# ============================================================
# 运行基准测试
# ============================================================

echo "🚀 Running benchmark tests..."
echo ""

BENCHMARK_EXE="./benchmark_comparison"
OUTPUT_FILE="$OUTPUT_DIR/benchmark_$TIMESTAMP.json"

if [ ! -f "$BENCHMARK_EXE" ]; then
    echo "❌ Benchmark executable not found: $BENCHMARK_EXE"
    exit 1
fi

"$BENCHMARK_EXE" \
    --model "$TARGET_MODEL" \
    --model-draft "$DRAFT_MODEL" \
    --output "$OUTPUT_FILE" \
    --max-tokens 100 \
    --temperature 0.7

if [ $? -ne 0 ]; then
    echo "❌ Benchmark failed"
    exit 1
fi

echo ""
echo "✅ Benchmark complete!"
echo "📄 Results saved to: $OUTPUT_FILE"
echo ""

# ============================================================
# 生成报告
# ============================================================

echo "📊 Generating analysis report..."

ANALYSIS_SCRIPT="$SCRIPT_DIR/analyze_benchmark.py"

if [ -f "$ANALYSIS_SCRIPT" ]; then
    python3 "$ANALYSIS_SCRIPT" "$OUTPUT_FILE"

    if [ $? -eq 0 ]; then
        echo "✅ Analysis complete!"
    else
        echo "⚠️  Analysis script failed (non-critical)"
    fi
else
    echo "⚠️  Analysis script not found: $ANALYSIS_SCRIPT"
    echo "   Skipping automatic analysis"
fi

echo ""

# ============================================================
# 总结
# ============================================================

echo "╔════════════════════════════════════════════════════════════╗"
echo "║                    Benchmark Complete!                     ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "📁 Output files:"
echo "   Results (JSON):  $OUTPUT_FILE"

PNG_FILE="${OUTPUT_FILE%.json}_charts.png"
if [ -f "$PNG_FILE" ]; then
    echo "   Charts (PNG):    $PNG_FILE"
fi

REPORT_FILE="${OUTPUT_FILE%.json}_report.md"
if [ -f "$REPORT_FILE" ]; then
    echo "   Report (MD):     $REPORT_FILE"
fi

echo ""
echo "🎯 Next steps:"
echo "   1. Review the JSON results"
echo "   2. Check the generated charts (if available)"
echo "   3. Analyze performance metrics"
echo "   4. Update your thesis with experimental data"
echo ""
