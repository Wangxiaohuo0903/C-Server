#!/bin/bash

# 性能测试：Server-12 (无KV Cache) vs Server-13 (有KV Cache)

echo "========================================="
echo "Performance Test: Server-12 vs Server-13"
echo "========================================="
echo ""

SERVER_URL="http://localhost:8080"
CHAT_ID="perf-test-$(date +%s)"

# 测试函数：发送消息并计时
test_chat() {
    local msg="$1"
    local num="$2"

    echo -n "[$num] Sending: '$msg' ... "

    start=$(perl -MTime::HiRes=time -e 'print time')

    response=$(curl -s -X POST "$SERVER_URL/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"$msg\"}")

    end=$(perl -MTime::HiRes=time -e 'print time')
    duration=$(echo "$end - $start" | bc)

    echo "${duration}s"
    echo "    Response: $(echo $response | jq -r '.answer' | head -c 50)..."

    echo $duration
}

echo "🔵 Testing multi-turn conversation..."
echo ""

# 进行5轮对话
times=()
times+=($(test_chat "你好" 1))
times+=($(test_chat "介绍一下你自己" 2))
times+=($(test_chat "你能做什么" 3))
times+=($(test_chat "解释一下人工智能" 4))
times+=($(test_chat "总结一下我们聊了什么" 5))

echo ""
echo "⏱️  Response Times:"
for i in ${!times[@]}; do
    echo "  Round $((i+1)): ${times[$i]}s"
done

# 计算平均时间
total=0
for t in "${times[@]}"; do
    total=$(echo "$total + $t" | bc)
done
avg=$(echo "scale=2; $total / ${#times[@]}" | bc)

echo ""
echo "📊 Statistics:"
echo "  Total rounds: ${#times[@]}"
echo "  Average time: ${avg}s"
echo ""
