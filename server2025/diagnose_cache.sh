#!/bin/bash

# 诊断KV Cache失效原因
# 观察在第5轮（触发prune）前后的日志变化

echo "========================================="
echo "KV Cache Diagnostic Test"
echo "Observing cache behavior around pruning"
echo "========================================="
echo ""

SERVER13_URL="http://localhost:6061"
CHAT_ID="diagnose-$(date +%s)"

echo "Testing 10 rounds to observe pruning at round 5..."
echo ""

for i in {1..10}; do
    echo "=== Round $i ==="

    curl -s -X POST "$SERVER13_URL/infer" \
        -H 'Content-Type: application/json' \
        -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"Round $i\"}" > /dev/null

    # 等待处理完成
    sleep 0.5

    # 查看最新的Docker日志
    echo "Docker logs (last 10 lines):"
    docker logs server-13 2>&1 | tail -10
    echo ""
done

echo "========================================="
echo "Analysis:"
echo "- Rounds 1-4: Cache should grow"
echo "- Round 5: Should trigger prune (history > 8 messages)"
echo "- Round 6+: Cache should invalidate due to history change"
echo "========================================="
