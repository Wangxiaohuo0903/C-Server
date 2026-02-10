#!/bin/bash

echo "========================================="
echo "Server-14 多轮对话测试 (使用 /infer_v3)"
echo "========================================="
echo ""

TOKEN="eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VybmFtZSI6InRlc3R1c2VyNCIsImV4cCI6MTc2ODM1Njg2OH0.Cgk4vXYJZv1tUHNMhyPfKOLr6cGaaeP98NUX1K9zs5U"

# 第一步：创建新会话
echo "=== 步骤1: 创建新会话 ==="
SESSION_RESPONSE=$(curl -s -X POST http://localhost:6062/api/sessions/new \
  -H "Authorization: Bearer $TOKEN")
SESSION_ID=$(echo "$SESSION_RESPONSE" | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)

if [ -z "$SESSION_ID" ]; then
    echo "❌ 创建会话失败！"
    echo "响应: $SESSION_RESPONSE"
    exit 1
fi

echo "✓ 会话创建成功: $SESSION_ID"
echo ""

sleep 1

echo "=== 第1轮: 告诉AI我的名字 ==="
RESPONSE1=$(curl -s -X POST "http://localhost:6062/api/sessions/$SESSION_ID/infer_v3" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"message":"我的名字叫小明","max_tokens":100,"temperature":0.7}')
echo "$RESPONSE1" | head -c 200
echo "..."
echo ""

sleep 3

echo "=== 第2轮: 测试记忆（问AI我叫什么） ==="
RESPONSE2=$(curl -s -X POST "http://localhost:6062/api/sessions/$SESSION_ID/infer_v3" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"message":"我刚才说我叫什么？","max_tokens":100,"temperature":0.7}')
echo "$RESPONSE2" | head -c 200
echo "..."
echo ""

sleep 3

echo "=== 第3轮: 再次确认 ==="
RESPONSE3=$(curl -s -X POST "http://localhost:6062/api/sessions/$SESSION_ID/infer_v3" \
  -H "Authorization: Bearer $TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"message":"确认一下，我的名字是？","max_tokens":100,"temperature":0.7}')
echo "$RESPONSE3" | head -c 200
echo "..."
echo ""

echo "========================================="
echo "测试完成！"
echo ""
echo "检查点："
echo "✓ JWT认证"
echo "✓ 会话创建"
echo "✓ 多轮对话"
echo "✓ 会话记忆（如果第2/3轮回答了'小明'）"
echo "========================================="
