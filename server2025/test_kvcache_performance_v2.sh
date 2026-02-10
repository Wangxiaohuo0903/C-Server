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

    echo "[Round $num] Sending: '$msg'"

    start=$(perl -MTime::HiRes=time -e 'print time')

    response=$(curl -s -X POST "$SERVER_URL/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"$msg\"}")

    end=$(perl -MTime::HiRes=time -e 'print time')
    duration=$(echo "$end - $start" | bc)

    # 提取答案
    answer=$(echo "$response" | python3 -c "import sys, json; print(json.load(sys.stdin).get('answer', 'ERROR')[:60])" 2>/dev/null || echo "parse_error")

    echo "  Time: ${duration}s"
    echo "  Answer: $answer..."
    echo ""

    # 返回时间供数组使用
    echo "$duration" > /tmp/perf_time_$num.txt
}

# 进行5轮对话
test_chat "Hello" 1
test_chat "What is your name?" 2
test_chat "Tell me about AI" 3
test_chat "Explain machine learning" 4
test_chat "Summarize our conversation" 5

echo ""
echo "========================================="
echo "⏱️  Response Times:"
echo "========================================="

# 读取时间数据
times=()
for i in {1..5}; do
    if [ -f /tmp/perf_time_$i.txt ]; then
        t=$(cat /tmp/perf_time_$i.txt)
        times+=("$t")
        echo "  Round $i: ${t}s"
        rm /tmp/perf_time_$i.txt
    fi
done

# 计算平均时间
total=0
count=0
for t in "${times[@]}"; do
    if [[ "$t" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        total=$(echo "$total + $t" | bc)
        count=$((count + 1))
    fi
done

if [ $count -gt 0 ]; then
    avg=$(echo "scale=3; $total / $count" | bc)
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
