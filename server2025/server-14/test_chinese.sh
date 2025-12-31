#!/bin/bash
# test_chinese.sh

# 1. Create session
echo "Creating session..."
RESP=$(curl -s -X POST http://localhost:6060/api/sessions/new)
SID=$(echo $RESP | jq -r '.session_id')
echo "Session ID: $SID"

# 2. Send Chinese message and capture RAW output
echo "Sending '你好'..."
# We use output to file to preserve bytes
curl -s -X POST "http://localhost:6060/api/sessions/$SID/chat" \
     -H "Content-Type: application/json" \
     -d '{"message":"你好", "max_tokens": 20, "temperature": 0.1}' > response.bin

# 3. Show the response body text (might be garbage)
cat response.bin
echo -e "\n\n=== HEX DUMP ==="
# 4. Hex dump to check validity
xxd response.bin
