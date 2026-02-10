#!/bin/bash

echo "========================================="
echo "Server-14 多轮对话测试"
echo "========================================="
echo ""

TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6InRlc3R1c2VyNCIsImV4cCI6MTc2ODM1Njg2OH0.Cgk4vXYJZv1tUHNMhyPfKOLr6cGaaeP98NUX1K9zs5U"
CHAT_ID="multi-test-$(date +%s)"

echo "使用会话ID: $CHAT_ID"
echo ""

echo "=== 第1轮: 告诉AI我的名字 ==="
RESPONSE1=$(curl -s -X POST http://localhost:6062/infer \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"我的名字叫小明\"}")
echo "$RESPONSE1" | head -c 150
echo "..."
echo ""

sleep 3

echo "=== 第2轮: 测试记忆（问AI我叫什么） ==="
RESPONSE2=$(curl -s -X POST http://localhost:6062/infer \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"我刚才说我叫什么？\"}")
echo "$RESPONSE2" | head -c 150
echo "..."
echo ""

sleep 3

echo "=== 第3轮: 再次确认 ==="
RESPONSE3=$(curl -s -X POST http://localhost:6062/infer \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"chat_id\":\"$CHAT_ID\",\"prompt\":\"确认一下，我的名字是？\"}")
echo "$RESPONSE3" | head -c 150
echo "..."
echo ""

echo "========================================="
echo "测试完成！"
echo ""
echo "检查点："
echo "✓ JWT认证"
echo "✓ 多轮对话"
echo "✓ 会话记忆（如果第2/3轮回答了'小明'）"
echo "========================================="
