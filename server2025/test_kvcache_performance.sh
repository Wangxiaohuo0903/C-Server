#!/bin/bash

# KV Cache性能对比测试: Server-12 (无KV Cache) vs Server-13 (有KV Cache)

SERVER_URL="http://localhost:8080"
CHAT_ID="perf-test-$(date +%s)"

echo "========================================="
echo "KV Cache Performance Test"
echo "========================================="
echo ""
echo "Chat ID: $CHAT_ID"
echo "Testing with 5 rounds of conversation..."
echo ""

# 测试函数：发送消息并计时
test_chat() {
    local msg="$1"
    local num="$2"

    echo -n "[Round $num] '$msg' ... "

    start=$(perl -MTime::HiRes=time -e 'print time')

    response=$(curl -s -X POST "$SERVER_URL/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"$msg\"}")

    end=$(perl -MTime::HiRes=time -e 'print time')
    duration=$(echo "$end - $start" | bc)

    # 提取答案
    answer=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('answer', 'ERROR')[:50])" 2>/dev/null || echo "parse_error")

    echo "${duration}s"
    echo "      Answer: $answer..."
    echo ""

    echo "$duration"
}

# 进行5轮对话
times=()
times+=($(test_chat "Hello" 1))
times+=($(test_chat "What is your name?" 2))
times+=($(test_chat "Tell me about AI" 3))
times+=($(test_chat "Explain machine learning" 4))
times+=($(test_chat "Summarize our conversation" 5))

echo ""
echo "========================================="
echo "⏱️  Response Times:"
echo "========================================="
for i in ${!times[@]}; do
    echo "  Round $((i+1)): ${times[$i]}s"
done

# 计算平均时间
total=0
count=0
for t in "${times[@]}"; do
    # 跳过非数字的值
    if [[ "$t" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        total=$(echo "$total + $t" | bc)
        count=$((count + 1))
    fi
done

if [ $count -gt 0 ]; then
    avg=$(echo "scale=2; $total / $count" | bc)
    echo ""
    echo "📊 Statistics:"
    echo "  Total rounds: $count"
    echo "  Average time: ${avg}s"
    echo "  Total time: ${total}s"
else
    echo ""
    echo "⚠️  No valid timing data collected"
fi
echo ""
