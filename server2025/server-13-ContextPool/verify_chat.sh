#!/bin/bash
# verify_chat.sh

# 1. Create a new session
echo "Creating session..."
RESP=$(curl -s -X POST http://localhost:6060/api/sessions/new)
SID=$(echo $RESP | jq -r '.session_id')
echo "Session ID: $SID"

# 2. Send a chat message
echo "Sending message 'Hello'..."
# We use a simple prompt to see if it responds like a chatbot or continues text
curl -s -X POST "http://localhost:6060/api/sessions/$SID/chat" \
     -H "Content-Type: application/json" \
     -d '{"message":"Hello, who are you?", "max_tokens": 50, "temperature": 0.1}' | jq .
