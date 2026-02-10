#!/bin/bash

# 长对话测试 (15轮) - 展示KV Cache真正优势

echo "========================================="
echo "Long Conversation Test (15 rounds)"
echo "Server-12 vs Server-13"
echo "========================================="
echo ""

SERVER12_URL="http://localhost:6060"
SERVER13_URL="http://localhost:6061"
CHAT_ID="long-test-$(date +%s)"

# 测试函数
test_server() {
    local url="$1"
    local msg="$2"

    start=$(perl -MTime::HiRes=time -e 'print time')

    curl -s -X POST "$url/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"$msg\"}" > /dev/null

    end=$(perl -MTime::HiRes=time -e 'print time')
    duration=$(echo "$end - $start" | bc)

    echo "$duration"
}

# 15轮简短问题(减少生成时间,突出KV Cache优势)
questions=(
    "Hi"
    "Name?"
    "Age?"
    "Job?"
    "Skills?"
    "Hobbies?"
    "Location?"
    "Education?"
    "Experience?"
    "Languages?"
    "Projects?"
    "Goals?"
    "Interests?"
    "Achievements?"
    "Summary?"
)

echo "🔵 Server-12 (No KV Cache):"
server12_times=()
for i in "${!questions[@]}"; do
    round=$((i+1))
    echo -n "  [$round/15] "
    time=$(test_server "$SERVER12_URL" "${questions[$i]}")
    server12_times+=("$time")
    echo "${time}s"
done

echo ""
echo "🟢 Server-13 (KV Cache):"
server13_times=()
for i in "${!questions[@]}"; do
    round=$((i+1))
    echo -n "  [$round/15] "
    time=$(test_server "$SERVER13_URL" "${questions[$i]}")
    server13_times+=("$time")
    echo "${time}s"
done

echo ""
echo "========================================="
echo "📊 Round-by-Round Comparison"
echo "========================================="
printf "%-6s %-12s %-12s %-10s\n" "Round" "Server-12" "Server-13" "Speedup"
echo "-------------------------------------------"

for i in "${!questions[@]}"; do
    round=$((i+1))
    time12="${server12_times[$i]}"
    time13="${server13_times[$i]}"

    if [[ "$time12" =~ ^[0-9]+\.?[0-9]*$ ]] && [[ "$time13" =~ ^[0-9]+\.?[0-9]*$ ]]; then
        speedup=$(echo "scale=2; $time12 / $time13" | bc)
        printf "%-6d %-12s %-12s %-10s\n" "$round" "${time12}s" "${time13}s" "${speedup}x"
    fi
done

# 分段统计
echo ""
echo "📈 Performance by Stage:"
echo ""

calc_avg() {
    local start=$1
    local end=$2
    local times12=("${!3}")
    local times13=("${!4}")

    local sum12=0
    local sum13=0
    local count=0

    for ((i=start; i<=end; i++)); do
        t12="${times12[$i]}"
        t13="${times13[$i]}"
        if [[ "$t12" =~ ^[0-9]+\.?[0-9]*$ ]] && [[ "$t13" =~ ^[0-9]+\.?[0-9]*$ ]]; then
            sum12=$(echo "$sum12 + $t12" | bc)
            sum13=$(echo "$sum13 + $t13" | bc)
            count=$((count + 1))
        fi
    done

    if [ $count -gt 0 ]; then
        avg12=$(echo "scale=3; $sum12 / $count" | bc)
        avg13=$(echo "scale=3; $sum13 / $count" | bc)
        speedup=$(echo "scale=2; $avg12 / $avg13" | bc)
        echo "  Rounds $((start+1))-$((end+1)): Server-12=${avg12}s, Server-13=${avg13}s, Speedup=${speedup}x"
    fi
}

calc_avg 0 4 server12_times[@] server13_times[@]
calc_avg 5 9 server12_times[@] server13_times[@]
calc_avg 10 14 server12_times[@] server13_times[@]

echo ""
