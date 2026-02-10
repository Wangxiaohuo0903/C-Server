#!/bin/bash

# Server-12 (无KV Cache) vs Server-13 (KV Cache) 性能对比测试

echo "========================================="
echo "Performance Test: Server-12 vs Server-13"
echo "========================================="
echo ""

SERVER12_URL="http://localhost:6060"
SERVER13_URL="http://localhost:6061"
CHAT_ID="perf-test-$(date +%s)"

# 测试函数
test_server() {
    local url="$1"
    local server_name="$2"
    local msg="$3"
    local round="$4"

    start=$(perl -MTime::HiRes=time -e 'print time')

    response=$(curl -s -X POST "$url/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"$msg\"}")

    end=$(perl -MTime::HiRes=time -e 'print time')
    duration=$(echo "$end - $start" | bc)

    echo "$duration"
}

# 5轮对话测试
questions=(
    "Hello"
    "What is your name?"
    "Tell me about AI"
    "Explain machine learning"
    "Summarize our conversation"
)

echo "🔵 Testing Server-12 (No KV Cache) on port 6060..."
echo ""
server12_times=()
for i in "${!questions[@]}"; do
    round=$((i+1))
    echo -n "  Round $round: '${questions[$i]}' ... "
    time=$(test_server "$SERVER12_URL" "Server-12" "${questions[$i]}" "$round")
    server12_times+=("$time")
    echo "${time}s"
done

echo ""
echo "🟢 Testing Server-13 (KV Cache) on port 6061..."
echo ""
server13_times=()
for i in "${!questions[@]}"; do
    round=$((i+1))
    echo -n "  Round $round: '${questions[$i]}' ... "
    time=$(test_server "$SERVER13_URL" "Server-13" "${questions[$i]}" "$round")
    server13_times+=("$time")
    echo "${time}s"
done

echo ""
echo "========================================="
echo "📊 Performance Comparison Results"
echo "========================================="
echo ""
printf "%-8s %-15s %-15s %-15s\n" "Round" "Server-12" "Server-13" "Speedup"
printf "%-8s %-15s %-15s %-15s\n" "-----" "---------" "---------" "-------"

for i in "${!questions[@]}"; do
    round=$((i+1))
    time12="${server12_times[$i]}"
    time13="${server13_times[$i]}"

    if [[ "$time12" =~ ^[0-9]+\.?[0-9]*$ ]] && [[ "$time13" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        speedup=$(echo "scale=2; $time12 / $time13" | bc)
        printf "%-8d %-15s %-15s %-15s\n" "$round" "${time12}s" "${time13}s" "${speedup}x"
    fi
done

# 计算平均值
total12=0
total13=0
count=0
for i in "${!server12_times[@]}"; do
    t12="${server12_times[$i]}"
    t13="${server13_times[$i]}"
    if [[ "$t12" =~ ^[0-9]+\.?[0-9]*$ ]] && [[ "$t13" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        total12=$(echo "$total12 + $t12" | bc)
        total13=$(echo "$total13 + $t13" | bc)
        count=$((count + 1))
    fi
done

if [ $count -gt 0 ]; then
    avg12=$(echo "scale=3; $total12 / $count" | bc)
    avg13=$(echo "scale=3; $total13 / $count" | bc)
    avg_speedup=$(echo "scale=2; $avg12 / $avg13" | bc)

    echo ""
    echo "Average:"
    printf "%-8s %-15s %-15s %-15s\n" "" "${avg12}s" "${avg13}s" "${avg_speedup}x"
    echo ""
    echo "🎉 Server-13 (KV Cache) is ${avg_speedup}x faster on average!"
fi

echo ""
