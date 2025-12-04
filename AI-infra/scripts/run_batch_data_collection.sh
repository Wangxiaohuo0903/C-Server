#!/bin/bash
#
# Phase 2 批量数据收集脚本
# 目标: 从 n=6 扩展到 n≥30
#
# 实验设计:
#   - 6种任务类型 × 5次重复 = 30个数据点
#   - 固定配置: enable_confidence_guide = true
#   - 固定模型: TinyLlama Q4 (draft + target 相同)
#   - 固定采样: temperature = 0.0 (greedy)
#

WORKSPACE="/workspace"
BUILD_DIR="$WORKSPACE/AI-chats-linux/build"
MODEL="$WORKSPACE/models/tinyllama-q4.gguf"
RESULTS_DIR="$WORKSPACE/batch_collection_results"

# 任务类型列表
TASK_TYPES=(
  "CODE_GENERATION"
  "JSON_GENERATION"
  "QA_CONVERSATION"
  "CREATIVE_WRITING"
  "MATH_REASONING"
  "TRANSLATION"
)

# 重复次数
NUM_REPEATS=5

echo "========================================================"
echo "  Phase 2 批量数据收集 (n=6 → n=30)"
echo "========================================================"
echo ""
echo "配置信息:"
echo "  - 任务类型数: ${#TASK_TYPES[@]}"
echo "  - 每种任务重复次数: $NUM_REPEATS"
echo "  - 总目标数据点数: $((${#TASK_TYPES[@]} * NUM_REPEATS))"
echo "  - 置信度引导: 已启用"
echo "  - 模型: TinyLlama Q4"
echo ""

# 创建结果目录
mkdir -p "$RESULTS_DIR"

# 记录开始时间
START_TIME=$(date +%s)
echo "实验开始时间: $(date)"
echo ""

# 初始化计数器
TOTAL_TESTS=0
SUCCESS_COUNT=0
FAIL_COUNT=0

# 主循环: 遍历每种任务类型
for TASK_TYPE in "${TASK_TYPES[@]}"; do
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo "任务类型: $TASK_TYPE"
  echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
  echo ""

  # 重复测试
  for RUN in $(seq 1 $NUM_REPEATS); do
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo "[$TOTAL_TESTS/30] 运行: $TASK_TYPE - 第 $RUN 次"

    # 生成输出文件名
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    OUTPUT_LOG="$RESULTS_DIR/${TASK_TYPE}_run${RUN}_${TIMESTAMP}.log"

    # 运行测试 (使用test_task_aware with --task-type filter)
    cd "$BUILD_DIR"
    timeout 600 ./test_task_aware \
      --model "$MODEL" \
      --model-draft "$MODEL" \
      --task-type "$TASK_TYPE" \
      --run-number "$RUN" \
      2>&1 | tee "$OUTPUT_LOG"

    EXIT_CODE=$?

    if [ $EXIT_CODE -eq 0 ]; then
      echo "✅ 测试成功"
      SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    else
      echo "❌ 测试失败 (退出码: $EXIT_CODE)"
      FAIL_COUNT=$((FAIL_COUNT + 1))
    fi

    echo ""

    # 短暂延迟,避免连续测试导致系统资源压力
    sleep 2
  done

  echo ""
done

# 计算总耗时
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
HOURS=$((DURATION / 3600))
MINUTES=$(((DURATION % 3600) / 60))
SECONDS=$((DURATION % 60))

echo "========================================================"
echo "  批量数据收集完成"
echo "========================================================"
echo "总测试数: $TOTAL_TESTS"
echo "成功: $SUCCESS_COUNT"
echo "失败: $FAIL_COUNT"
echo "成功率: $(awk "BEGIN {printf \"%.1f\", ($SUCCESS_COUNT/$TOTAL_TESTS)*100}")%"
echo ""
echo "总耗时: ${HOURS}h ${MINUTES}m ${SECONDS}s"
echo ""
echo "结果文件目录: $RESULTS_DIR"
echo ""
echo "下一步:"
echo "  1. 运行数据提取脚本: python3 scripts/extract_batch_data.py"
echo "  2. 计算Pearson相关系数: python3 scripts/analyze_correlation.py"
echo "  3. 生成可视化图表: python3 scripts/plot_correlation.py"
echo ""
