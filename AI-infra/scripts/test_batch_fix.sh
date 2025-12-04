#!/bin/bash
#
# Batch Size修复验证测试脚本
# 用于在本地Linux/WSL环境中编译和测试
#
# 使用方法:
#   chmod +x test_batch_fix.sh
#   ./test_batch_fix.sh
#

set -e  # 遇到错误立即退出

echo "╔══════════════════════════════════════════════════════════╗"
echo "║     Batch Size修复 - 编译和测试脚本                     ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# 检测操作系统
if [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
    echo "❌ 此脚本需要在Linux/WSL环境中运行"
    echo "请使用: wsl bash test_batch_fix.sh"
    exit 1
fi

# 切换到项目目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
AI_CHATS_DIR="$PROJECT_ROOT/AI-chats-linux"

cd "$AI_CHATS_DIR"
echo "📁 项目目录: $AI_CHATS_DIR"
echo ""

# Step 1: 检查依赖
echo "════════════════════════════════════════════════════════════"
echo "  Step 1: 检查编译依赖"
echo "════════════════════════════════════════════════════════════"

if ! command -v cmake &> /dev/null; then
    echo "❌ CMake未安装"
    echo "请运行: sudo apt-get install cmake build-essential"
    exit 1
fi

if ! command -v g++ &> /dev/null; then
    echo "❌ g++未安装"
    echo "请运行: sudo apt-get install build-essential"
    exit 1
fi

echo "✅ cmake: $(cmake --version | head -1)"
echo "✅ g++: $(g++ --version | head -1)"
echo ""

# Step 2: 清理旧编译文件
echo "════════════════════════════════════════════════════════════"
echo "  Step 2: 清理旧编译文件"
echo "════════════════════════════════════════════════════════════"

cd build
echo "🗑️  删除旧的test_task_aware..."
rm -f test_task_aware

echo "🗑️  删除旧的object文件..."
rm -f CMakeFiles/test_task_aware.dir/test_task_aware.cpp.o
find CMakeFiles/llama.dir/src/inference/ -name "*.o" -delete 2>/dev/null || true

echo "✅ 清理完成"
echo ""

# Step 3: 重新编译
echo "════════════════════════════════════════════════════════════"
echo "  Step 3: 重新编译 (包含batch size修复)"
echo "════════════════════════════════════════════════════════════"

echo "🔨 开始编译..."
echo ""

if make test_task_aware -j$(nproc); then
    echo ""
    echo "✅ 编译成功"
else
    echo ""
    echo "❌ 编译失败"
    exit 1
fi

echo ""

# Step 4: 验证编译结果
echo "════════════════════════════════════════════════════════════"
echo "  Step 4: 验证编译结果"
echo "════════════════════════════════════════════════════════════"

if [ -f "test_task_aware" ]; then
    echo "✅ test_task_aware 已生成"
    ls -lh test_task_aware
else
    echo "❌ test_task_aware 未生成"
    exit 1
fi

echo ""

# Step 5: 检查模型文件
echo "════════════════════════════════════════════════════════════"
echo "  Step 5: 检查模型文件"
echo "════════════════════════════════════════════════════════════"

MODEL_PATH="../models/tinyllama-q4.gguf"

if [ -f "$MODEL_PATH" ]; then
    echo "✅ 模型文件存在: $MODEL_PATH"
    ls -lh "$MODEL_PATH"
else
    echo "❌ 模型文件不存在: $MODEL_PATH"
    echo "请下载TinyLlama模型并放置到 models/ 目录"
    exit 1
fi

echo ""

# Step 6: 运行测试
echo "════════════════════════════════════════════════════════════"
echo "  Step 6: 运行完整测试"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "⏱️  预计运行时间: 3-4分钟"
echo "📋 测试内容:"
echo "  - Part 1: 13个任务分类测试"
echo "  - Part 2: 6个完整推理测试"
echo ""
echo "🚀 开始测试..."
echo ""

LOG_FILE="../BATCH_FIX_VERIFIED_$(date +%Y%m%d_%H%M%S).log"

./test_task_aware \
  --model "$MODEL_PATH" \
  --model-draft "$MODEL_PATH" \
2>&1 | tee "$LOG_FILE"

EXIT_CODE=$?

echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Step 7: 测试结果总结"
echo "════════════════════════════════════════════════════════════"
echo ""

if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ 测试完成，无崩溃"
else
    echo "❌ 测试异常退出 (exit code: $EXIT_CODE)"
fi

echo ""
echo "📊 关键指标:"
echo ""

# 提取关键结果
grep "Classification Accuracy:" "$LOG_FILE" || echo "  ⚠️  未找到分类准确率"
echo ""

echo "Part 2 - 推理结果:"
echo ""

# 提取每个测试的token生成数量
for task in "code_generation_python" "qa_general" "creative_poetry" "json_user_data" "math_calculation" "translation_zh_en"; do
    echo "  $task:"
    grep -A 10 "Test: $task (Full Inference)" "$LOG_FILE" | grep "Tokens generated:" | head -1 || echo "    ⚠️  未找到"
    grep -A 10 "Test: $task (Full Inference)" "$LOG_FILE" | grep "Accept rate:" | head -1 || echo "    ⚠️  未找到"
    echo ""
done

echo ""
echo "📄 完整日志已保存到: $LOG_FILE"
echo ""

# Step 8: 验证修复效果
echo "════════════════════════════════════════════════════════════"
echo "  Step 8: 验证修复效果"
echo "════════════════════════════════════════════════════════════"
echo ""

# 检查是否有GGML_ASSERT错误
if grep -q "GGML_ASSERT.*failed" "$LOG_FILE"; then
    echo "❌ 发现GGML_ASSERT错误 - batch size修复未生效！"
    echo ""
    grep "GGML_ASSERT" "$LOG_FILE"
    echo ""
    echo "可能原因:"
    echo "  1. 代码修改未正确应用"
    echo "  2. 编译时使用了缓存的object文件"
    echo ""
    echo "建议: 完全清理build目录后重新编译"
    exit 1
else
    echo "✅ 没有batch size错误"
fi

# 检查code_generation是否生成足够的tokens
CODE_TOKENS=$(grep -A 10 "Test: code_generation_python (Full Inference)" "$LOG_FILE" | grep "Tokens generated:" | awk '{print $3}' || echo "0")
if [ "$CODE_TOKENS" -gt 40 ]; then
    echo "✅ code_generation生成了 $CODE_TOKENS tokens (>40)"
else
    echo "⚠️  code_generation只生成了 $CODE_TOKENS tokens (<40)"
fi

# 检查qa_general是否生成足够的tokens
QA_TOKENS=$(grep -A 10 "Test: qa_general (Full Inference)" "$LOG_FILE" | grep "Tokens generated:" | awk '{print $3}' || echo "0")
if [ "$QA_TOKENS" -gt 40 ]; then
    echo "✅ qa_general生成了 $QA_TOKENS tokens (>40)"
else
    echo "⚠️  qa_general只生成了 $QA_TOKENS tokens (<40)"
fi

echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                    测试完成！                            ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "下一步:"
echo "  1. 查看完整日志: cat $LOG_FILE"
echo "  2. 如果修复成功，继续Phase 1.2: 优化翻译任务"
echo "  3. 如果仍有问题，检查代码修改是否正确应用"
echo ""
