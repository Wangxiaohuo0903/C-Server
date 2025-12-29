#!/bin/bash

echo "=== Server-13 Performance Benchmark ==="

# Install jq if not present (already in Dockerfile but just in case)
if ! command -v jq &> /dev/null; then
    apt-get update && apt-get install -y jq
fi

# 测试1：单会话多轮对话
echo -e "\n[Test 1] Multi-turn conversation (10 rounds)"
# Get session ID
RESP=$(curl -s -X POST http://localhost:8080/api/sessions/new)
SESSION_ID=$(echo $RESP | jq -r '.session_id')
echo "Session ID: $SESSION_ID"

if [ "$SESSION_ID" == "null" ] || [ -z "$SESSION_ID" ]; then
    echo "Failed to create session. Response: $RESP"
    exit 1
fi

for i in {1..5}; do
    START=$(date +%s%3N)
    curl -s -X POST "http://localhost:8080/api/sessions/$SESSION_ID/chat" \
         -H "Content-Type: application/json" \
         -d "{\"message\":\"Round $i: Tell me a short story about number $i\",\"max_tokens\":20}" > /dev/null
    END=$(date +%s%3N)
    ELAPSED=$((END - START))
    echo "Round $i: ${ELAPSED}ms"
done

# 测试2：并发推理请求 (Batch Inference Test)
echo -e "\n[Test 2] Concurrent INFERENCE requests (4 parallel)"

# Create 4 sessions first
SIDS=()
for i in {1..4}; do
    RESP=$(curl -s -X POST http://localhost:8080/api/sessions/new)
    SID=$(echo $RESP | jq -r '.session_id')
    SIDS+=($SID)
done

echo "Created sessions: ${SIDS[*]}"

START=$(date +%s%3N)

for SID in "${SIDS[@]}"; do
    (curl -s -X POST "http://localhost:8080/api/sessions/$SID/chat" \
          -H "Content-Type: application/json" \
          -d "{\"message\":\"What is $SID?\",\"max_tokens\":10}" > /dev/null) &
done

wait
END=$(date +%s%3N)
ELAPSED=$((END - START))
echo "Total time for 4 concurrent inference requests: ${ELAPSED}ms"
echo "Average per request: $((ELAPSED / 4))ms"
