#!/bin/bash

# Phase 2 对比实验脚本
# 比较启用/禁用置信度引导的性能差异

WORKSPACE="/workspace"
BUILD_DIR="$WORKSPACE/AI-chats-linux/build"
MODEL="$WORKSPACE/models/tinyllama-q4.gguf"
RESULTS_DIR="$WORKSPACE/comparison_results"

echo "======================================================"
echo "  Phase 2 对比实验 - 置信度引导 vs 基线"
echo "======================================================"
echo ""

# 创建结果目录
mkdir -p "$RESULTS_DIR"

# 记录实验开始时间
START_TIME=$(date +%s)
echo "实验开始时间: $(date)"
echo ""

# ============================================
# 实验1: 无置信度引导 (基线)
# ============================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "实验1: 无置信度引导 (baseline)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# 确保test_task_aware.cpp中 enable_confidence_guide = false
echo "配置: enable_confidence_guide = false"
sed -i 's/config.enable_confidence_guide = true/config.enable_confidence_guide = false/' \
    "$WORKSPACE/AI-chats-linux/test_task_aware.cpp"

# 重新编译
echo "重新编译..."
cd "$BUILD_DIR"
make test_task_aware -j4 > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译成功"
echo ""

# 运行测试
echo "运行基线测试 (无置信度引导)..."
./test_task_aware \
    --model "$MODEL" \
    --model-draft "$MODEL" \
    2>&1 | tee "$RESULTS_DIR/baseline_no_confidence.log"

BASELINE_EXIT=$?
echo ""
echo "基线测试完成 (退出码: $BASELINE_EXIT)"
echo ""

# ============================================
# 实验2: 启用置信度引导
# ============================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "实验2: 启用置信度引导"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# 启用置信度引导
echo "配置: enable_confidence_guide = true"
sed -i 's/config.enable_confidence_guide = false/config.enable_confidence_guide = true/' \
    "$WORKSPACE/AI-chats-linux/test_task_aware.cpp"

# 重新编译
echo "重新编译..."
cd "$BUILD_DIR"
make test_task_aware -j4 > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ 编译失败"
    exit 1
fi

echo "✅ 编译成功"
echo ""

# 运行测试
echo "运行实验测试 (启用置信度引导)..."
./test_task_aware \
    --model "$MODEL" \
    --model-draft "$MODEL" \
    2>&1 | tee "$RESULTS_DIR/with_confidence.log"

EXPERIMENT_EXIT=$?
echo ""
echo "实验测试完成 (退出码: $EXPERIMENT_EXIT)"
echo ""

# ============================================
# 生成对比报告
# ============================================
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))

echo "======================================================"
echo "  实验完成"
echo "======================================================"
echo "总耗时: ${DURATION}秒"
echo ""
echo "结果文件:"
echo "  - 基线: $RESULTS_DIR/baseline_no_confidence.log"
echo "  - 实验: $RESULTS_DIR/with_confidence.log"
echo ""
echo "下一步: 运行分析脚本提取性能指标"
echo "  python3 scripts/compare_results.py"
echo ""
