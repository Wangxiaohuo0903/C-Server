#!/bin/bash

# Server-12 vs Server-13-KVcache 性能对比测试
# 测试多轮对话的响应时间差异

SERVER12_PORT=7012
SERVER13_PORT=7013

echo "======================================"
echo "Server-12 vs Server-13 Performance Test"
echo "======================================"
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 等待服务器启动
wait_for_server() {
    local port=$1
    local name=$2
    echo -n "Waiting for $name to start..."
    for i in {1..60}; do
        if curl -s http://localhost:$port/api/health > /dev/null 2>&1; then
            echo -e " ${GREEN}✓${NC}"
            return 0
        fi
        echo -n "."
        sleep 2
    done
    echo -e " ${RED}✗ Timeout${NC}"
    return 1
}

# 创建会话
create_session() {
    local port=$1
    curl -s -X POST "http://localhost:$port/api/session/create" | jq -r '.session_id'
}

# 发送消息并测量时间
send_message() {
    local port=$1
    local session_id=$2
    local message=$3

    local start=$(date +%s.%N)
    local response=$(curl -s -X POST "http://localhost:$port/api/chat" \
        -H "Content-Type: application/json" \
        -d "{\"session_id\":\"$session_id\",\"message\":\"$message\"}")
    local end=$(date +%s.%N)

    local duration=$(echo "$end - $start" | bc)
    echo "$duration"
}

# 测试多轮对话
test_conversation() {
    local port=$1
    local server_name=$2

    echo ""
    echo "Testing $server_name (port $port)..."
    echo "--------------------------------"

    # 创建会话
    local session_id=$(create_session $port)
    if [ -z "$session_id" ]; then
        echo -e "${RED}Failed to create session${NC}"
        return 1
    fi
    echo "Session ID: $session_id"

    # 测试消息列表
    local messages=(
        "你好"
        "介绍一下北京"
        "那上海呢"
        "对比一下这两个城市"
        "哪个城市更适合旅游"
    )

    local total_time=0
    local round=1

    for msg in "${messages[@]}"; do
        echo -n "Round $round: '$msg' ... "
        local time=$(send_message $port "$session_id" "$msg")

        if [ $? -eq 0 ]; then
            echo -e "${GREEN}${time}s${NC}"
            total_time=$(echo "$total_time + $time" | bc)
        else
            echo -e "${RED}Failed${NC}"
        fi

        ((round++))
        sleep 1
    done

    echo ""
    echo "Total time: ${total_time}s"
    echo "$total_time" > /tmp/perf_${server_name}.txt
}

# 检查服务器是否就绪
echo "Checking server availability..."
if ! wait_for_server $SERVER12_PORT "Server-12"; then
    echo -e "${YELLOW}Warning: Server-12 not ready, skipping test${NC}"
    SERVER12_READY=false
else
    SERVER12_READY=true
fi

if ! wait_for_server $SERVER13_PORT "Server-13"; then
    echo -e "${YELLOW}Warning: Server-13 not ready, skipping test${NC}"
    SERVER13_READY=false
else
    SERVER13_READY=true
fi

# 运行测试
if [ "$SERVER12_READY" = true ]; then
    test_conversation $SERVER12_PORT "Server12"
fi

if [ "$SERVER13_READY" = true ]; then
    test_conversation $SERVER13_PORT "Server13"
fi

# 对比结果
echo ""
echo "======================================"
echo "Performance Comparison"
echo "======================================"

if [ "$SERVER12_READY" = true ] && [ "$SERVER13_READY" = true ]; then
    time12=$(cat /tmp/perf_Server12.txt)
    time13=$(cat /tmp/perf_Server13.txt)

    speedup=$(echo "scale=2; $time12 / $time13" | bc)
    improvement=$(echo "scale=1; ($time12 - $time13) / $time12 * 100" | bc)

    echo "Server-12 Total: ${time12}s"
    echo "Server-13 Total: ${time13}s"
    echo ""
    echo -e "${GREEN}Speedup: ${speedup}x${NC}"
    echo -e "${GREEN}Improvement: ${improvement}%${NC}"

    if (( $(echo "$speedup > 2" | bc -l) )); then
        echo -e "${GREEN}✓ Server-13 KV Cache optimization is working!${NC}"
    else
        echo -e "${YELLOW}⚠ Speedup is less than expected${NC}"
    fi
else
    echo -e "${YELLOW}Cannot compare: one or both servers not ready${NC}"
fi

# 清理
rm -f /tmp/perf_Server12.txt /tmp/perf_Server13.txt

echo ""
echo "Test completed!"
