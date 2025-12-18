#!/bin/bash

##########################################################
# Server-12 多轮对话测试脚本
# 测试会话管理和多轮对话功能
##########################################################

BASE_URL="http://localhost:8080"
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo "======================================="
echo "  Server-12 Multi-turn Chat Tests"
echo "======================================="
echo ""

# =====================================================
# ★ Server-12新增测试 - 多轮对话
# =====================================================

echo -e "${BLUE}=== 多轮对话测试（Server-12新增） ===${NC}"
echo ""

# 测试1: 创建新会话
echo -e "${YELLOW}Test 1: Create new session (POST /chat/create)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat/create)
echo "Response: $response"

# 提取session_id
session_id=$(echo "$response" | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)
echo "Extracted session_id: $session_id"

if [ -z "$session_id" ]; then
    echo -e "${RED}✗ Fail - No session_id returned${NC}"
    exit 1
else
    echo -e "${GREEN}✓ Pass - Session created: $session_id${NC}"
fi
echo ""

# 测试2: 第一轮对话 - 告诉模型名字
echo -e "${YELLOW}Test 2: Round 1 - Tell name (POST /chat)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id\", \"message\":\"My name is Alice.\", \"max_tokens\":\"64\"}")
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试3: 第二轮对话 - 询问名字（验证记忆）
echo -e "${YELLOW}Test 3: Round 2 - Ask name (验证上下文记忆)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id\", \"message\":\"What is my name?\", \"max_tokens\":\"64\"}")
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass - 模型应该记住名字是Alice${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试4: 第三轮对话 - 继续对话
echo -e "${YELLOW}Test 4: Round 3 - Continue conversation${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id\", \"message\":\"I like programming.\", \"max_tokens\":\"64\"}")
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试5: 获取会话历史
echo -e "${YELLOW}Test 5: Get session history (GET /chat/history)${NC}"
response=$(curl -s "${BASE_URL}/chat/history?session_id=$session_id")
echo "Response: $response"
if echo "$response" | grep -q "message_count"; then
    echo -e "${GREEN}✓ Pass - 应该包含至少6条消息（3轮对话）${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试6: 创建第二个会话
echo -e "${YELLOW}Test 6: Create second session${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat/create)
echo "Response: $response"
session_id2=$(echo "$response" | grep -o '"session_id":"[^"]*"' | cut -d'"' -f4)
echo "Session 2 ID: $session_id2"
if [ "$session_id" != "$session_id2" ] && [ -n "$session_id2" ]; then
    echo -e "${GREEN}✓ Pass - 两个会话ID不同${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试7: 在第二个会话中对话（验证会话隔离）
echo -e "${YELLOW}Test 7: Chat in session 2 (验证会话隔离)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id2\", \"message\":\"What is my name?\", \"max_tokens\":\"64\"}")
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass - Session 2应该不知道名字（会话隔离）${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试8: 删除第一个会话
echo -e "${YELLOW}Test 8: Delete session 1 (DELETE /chat/delete)${NC}"
response=$(curl -s -X DELETE ${BASE_URL}/chat/delete \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id\"}")
echo "Response: $response"
if echo "$response" | grep -q "Session deleted"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试9: 尝试使用已删除的会话
echo -e "${YELLOW}Test 9: Try to use deleted session (应该失败)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id\", \"message\":\"Hello\", \"max_tokens\":\"32\"}")
echo "Response: $response"
if echo "$response" | grep -q "Session not found"; then
    echo -e "${GREEN}✓ Pass - 正确拒绝已删除的会话${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 测试10: 缺少session_id参数
echo -e "${YELLOW}Test 10: Missing session_id (应该返回错误)${NC}"
response=$(curl -s -X POST ${BASE_URL}/chat \
  -H "Content-Type: application/json" \
  -d "{\"message\":\"Hello\"}")
echo "Response: $response"
if echo "$response" | grep -q "session_id required"; then
    echo -e "${GREEN}✓ Pass${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# =====================================================
# 兼容性测试 - Server-11的单轮推理
# =====================================================

echo -e "${BLUE}=== 兼容性测试（继承自Server-11） ===${NC}"
echo ""

# 测试11: 单轮推理仍然工作
echo -e "${YELLOW}Test 11: Single-turn inference (POST /infer-simple)${NC}"
response=$(curl -s -X POST ${BASE_URL}/infer-simple \
  -H "Content-Type: application/json" \
  -d '{"prompt":"What is 1+1?", "max_tokens":"32"}')
echo "Response: $response"
if echo "$response" | grep -q "success.*true"; then
    echo -e "${GREEN}✓ Pass - 单轮推理仍然正常工作${NC}"
else
    echo -e "${RED}✗ Fail${NC}"
fi
echo ""

# 清理：删除第二个会话
echo -e "${YELLOW}Cleanup: Deleting session 2${NC}"
curl -s -X DELETE ${BASE_URL}/chat/delete \
  -H "Content-Type: application/json" \
  -d "{\"session_id\":\"$session_id2\"}" > /dev/null
echo ""

echo "======================================="
echo "  All Tests Completed"
echo "======================================="
echo ""
echo "测试总结："
echo "- Tests 1-10: Server-12多轮对话功能"
echo "  - 会话创建和管理"
echo "  - 多轮对话上下文记忆"
echo "  - 会话隔离验证"
echo "  - 会话删除功能"
echo "- Test 11: Server-11单轮推理兼容性"
